#include "saiopt/block.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <optional>

namespace saiopt {

Variable Block::add_var(std::string name, VariableDef def) {
    std::size_t index = vars_.size();
    Variable variable(id_, index);
    auto [it, inserted] = var_names_.emplace(std::move(name), variable);
    if (!inserted) {
        throw std::runtime_error("a variable named \"" + it->first + "\" already exists in this block");
    }
    vars_.push_back(VarData{def.kind_, def.lower_, def.upper_});
    return variable;
}

ConstraintRef Block::add_constraint(std::string name, Constraint constraint) {
    std::size_t index = constraints_.size();
    ConstraintRef ref(id_, index);
    auto [it, inserted] = constraint_names_.emplace(name, ref);
    (void)it;
    if (!inserted) {
        throw std::runtime_error("a constraint named \"" + name + "\" already exists in this block");
    }
    constraints_.push_back(ConstraintData{std::move(constraint)});
    return ref;
}

void Block::add_sos2(std::string name, std::vector<Variable> vars) {
    if (vars.size() < 2) {
        throw std::runtime_error("the SOS2 set \"" + name + "\" needs at least two variables");
    }
    auto [it, inserted] = sos2_names_.emplace(name, sos2_.size());
    (void)it;
    if (!inserted) {
        throw std::runtime_error("an SOS2 set named \"" + name + "\" already exists in this block");
    }
    sos2_.push_back(Sos2Data{std::move(vars)});
}

void Block::set_objective(QuadraticExpression expr) { objective_ = std::move(expr); }

BlockRef Block::add_block(std::string name, Block child) {
    return add_block_scaled(std::move(name), std::move(child), 1.0);
}

BlockRef Block::add_block_scaled(std::string name, Block child, double multiplier) {
    std::size_t index = children_.size();
    BlockRef ref(index);
    auto [it, inserted] = child_names_.emplace(name, index);
    (void)it;
    if (!inserted) {
        throw std::runtime_error("a child block named \"" + name + "\" already exists in this block");
    }
    children_.push_back(ChildBlock{multiplier, std::move(child)});
    return ref;
}

const Block& Block::block(BlockRef ref) const { return children_.at(ref.index_).block; }

Block& Block::block(BlockRef ref) { return children_.at(ref.index_).block; }

std::optional<Variable> Block::var(const std::string& name) const {
    auto it = var_names_.find(name);
    if (it == var_names_.end()) return std::nullopt;
    return it->second;
}

std::vector<Variable> Block::vars(const std::string& name) const { return find_var_group(name).ordered; }

std::optional<ConstraintRef> Block::constraint(const std::string& name) const {
    auto it = constraint_names_.find(name);
    if (it == constraint_names_.end()) return std::nullopt;
    return it->second;
}

std::vector<ConstraintRef> Block::constraints(const std::string& name) const {
    return find_constraint_group(name).ordered;
}

const Block* Block::block(const std::string& name) const {
    auto it = child_names_.find(name);
    if (it == child_names_.end()) return nullptr;
    return &children_[it->second].block;
}

Block* Block::block(const std::string& name) {
    auto it = child_names_.find(name);
    if (it == child_names_.end()) return nullptr;
    return &children_[it->second].block;
}

std::vector<const Block*> Block::blocks(const std::string& name) const {
    const IndexedBlockGroup& group = find_block_group(name);
    std::vector<const Block*> result;
    result.reserve(group.ordered.size());
    for (BlockRef ref : group.ordered) result.push_back(&block(ref));
    return result;
}

std::vector<Block*> Block::blocks(const std::string& name) {
    const IndexedBlockGroup& group = find_block_group(name);
    std::vector<Block*> result;
    result.reserve(group.ordered.size());
    for (BlockRef ref : group.ordered) result.push_back(&block(ref));
    return result;
}

bool Block::find_var_path(const Block& block, Variable var, std::vector<std::uint64_t>& path) {
    if (block.id_ == var.block()) return true;
    for (std::size_t i = 0; i < block.children_.size(); ++i) {
        path.push_back(i);
        if (find_var_path(block.children_[i].block, var, path)) return true;
        path.pop_back();
    }
    return false;
}

namespace detail {

// Block::deserialize's way into the private state that the public API
// can't rebuild.
struct BlockReader {
    static void restore_piecewise(Block& block, std::string name, LinearExpression value) {
        block.restore_piecewise(std::move(name), std::move(value));
    }
    static void restore_group(Block& block, int kind, std::string name, const std::vector<std::uint64_t>& members,
                              const std::optional<std::vector<std::pair<KeyValue, std::uint64_t>>>& keys) {
        block.restore_group(kind, std::move(name), members, keys);
    }
};

}  // namespace detail

namespace {

void append_u8(std::vector<std::byte>& out, std::uint8_t v) { out.push_back(static_cast<std::byte>(v)); }

void append_u64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
    }
}

void append_f64(std::vector<std::byte>& out, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    append_u64(out, bits);
}

void append_string(std::vector<std::byte>& out, const std::string& s) {
    append_u64(out, s.size());
    const auto* data = reinterpret_cast<const std::byte*>(s.data());
    out.insert(out.end(), data, data + s.size());
}

// A key as a tag byte -- 0 integer, 1 float, 2 string, 3 tuple, the
// order of KeyValue's alternatives -- then its value; a tuple's value is
// its length and then each item.
void append_key(std::vector<std::byte>& out, const KeyValue& key) {
    append_u8(out, static_cast<std::uint8_t>(key.value.index()));
    if (const auto* i = std::get_if<std::int64_t>(&key.value)) {
        append_u64(out, static_cast<std::uint64_t>(*i));
    } else if (const auto* d = std::get_if<double>(&key.value)) {
        append_f64(out, *d);
    } else if (const auto* str = std::get_if<std::string>(&key.value)) {
        append_string(out, *str);
    } else {
        const auto& items = std::get<KeyValue::Tuple>(key.value).items;
        append_u64(out, items.size());
        for (const KeyValue& item : items) append_key(out, item);
    }
}

// Each group: its name, its members (as indices among the block's own
// variables, constraints, or children, via `index_of`) in declaration
// order, then a byte saying whether its keys follow, each with the index
// of its member.
template <typename Groups, typename IndexOf>
void append_groups(std::vector<std::byte>& out, const Groups& groups, IndexOf index_of) {
    append_u64(out, groups.size());
    for (const auto& [name, group] : groups) {
        append_string(out, name);
        append_u64(out, group.ordered.size());
        for (const auto& item : group.ordered) append_u64(out, index_of(item));
        auto keys = group.keys();
        append_u8(out, keys ? 1 : 0);
        if (!keys) continue;
        append_u64(out, keys->size());
        for (const auto& [key, item] : *keys) {
            append_key(out, key);
            append_u64(out, index_of(item));
        }
    }
}

// Reads primitives out of a buffer produced by the append_* helpers above,
// throwing std::runtime_error on a truncated or otherwise malformed buffer
// rather than reading out of bounds.
class Reader {
public:
    explicit Reader(const std::vector<std::byte>& buf) : buf_(buf) {}

    std::uint8_t read_u8() {
        require(1);
        return static_cast<std::uint8_t>(buf_[pos_++]);
    }

    std::uint64_t read_u64() {
        require(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(buf_[pos_++])) << (8 * i);
        }
        return v;
    }

    double read_f64() {
        std::uint64_t bits = read_u64();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    std::string read_string() {
        std::uint64_t len = read_u64();
        require(len);
        std::string s(reinterpret_cast<const char*>(&buf_[pos_]), static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return s;
    }

private:
    void require(std::uint64_t n) {
        if (pos_ + static_cast<std::size_t>(n) > buf_.size()) {
            throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
        }
    }

    const std::vector<std::byte>& buf_;
    std::size_t pos_ = 0;
};

// A Block rebuilt from wire data via the ordinary public API: the Block
// itself, plus the freshly-created Variable handles needed to resolve any
// sibling/ancestor constraint or objective that references one of them by
// (path, index) -- kept alongside `block` since `block` itself may already
// have been moved into a parent by the time such a reference is resolved.
struct RebuiltBlock {
    Block block;
    std::vector<Variable> own_vars;
    std::vector<RebuiltBlock> children;
};

Variable resolve_var_ref(const std::vector<Variable>& own_vars,
                          const std::vector<RebuiltBlock>& children,
                          const std::vector<std::uint64_t>& path, std::size_t path_pos,
                          std::uint64_t var_index) {
    if (path_pos == path.size()) {
        return own_vars.at(static_cast<std::size_t>(var_index));
    }
    const RebuiltBlock& next = children.at(static_cast<std::size_t>(path[path_pos]));
    return resolve_var_ref(next.own_vars, next.children, path, path_pos + 1, var_index);
}

Variable read_var_ref(Reader& reader, const std::vector<Variable>& own_vars,
                      const std::vector<RebuiltBlock>& children) {
    std::uint64_t path_len = reader.read_u64();
    std::vector<std::uint64_t> path(static_cast<std::size_t>(path_len));
    for (auto& p : path) p = reader.read_u64();
    std::uint64_t var_index = reader.read_u64();
    return resolve_var_ref(own_vars, children, path, 0, var_index);
}

LinearExpression read_expression(Reader& reader, const std::vector<Variable>& own_vars,
                                  const std::vector<RebuiltBlock>& children) {
    LinearExpression expr(reader.read_f64());
    std::uint64_t num_terms = reader.read_u64();
    for (std::uint64_t i = 0; i < num_terms; ++i) {
        Variable var = read_var_ref(reader, own_vars, children);
        double coeff = reader.read_f64();
        expr.set_coefficient(var, coeff);
    }
    return expr;
}

// A linear expression (see read_expression), followed by its quadratic
// terms: a count, then each term's two variable references and its
// coefficient.
QuadraticExpression read_quadratic_expression(Reader& reader, const std::vector<Variable>& own_vars,
                                              const std::vector<RebuiltBlock>& children) {
    QuadraticExpression expr(read_expression(reader, own_vars, children));
    std::uint64_t num_terms = reader.read_u64();
    for (std::uint64_t i = 0; i < num_terms; ++i) {
        Variable a = read_var_ref(reader, own_vars, children);
        Variable b = read_var_ref(reader, own_vars, children);
        double coeff = reader.read_f64();
        expr.set_coefficient(a, b, coeff);
    }
    return expr;
}

KeyValue read_key(Reader& reader, int depth = 0) {
    // Deep enough for any real key; bounds the recursion on a bad buffer.
    if (depth > 64) throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
    switch (reader.read_u8()) {
        case 0:
            return KeyValue{static_cast<std::int64_t>(reader.read_u64())};
        case 1:
            return KeyValue{reader.read_f64()};
        case 2:
            return KeyValue{reader.read_string()};
        case 3: {
            KeyValue::Tuple tuple;
            std::uint64_t size = reader.read_u64();
            for (std::uint64_t i = 0; i < size; ++i) tuple.items.push_back(read_key(reader, depth + 1));
            return KeyValue{std::move(tuple)};
        }
        default:
            throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
    }
}

RebuiltBlock read_block(Reader& reader) {
    Block block;

    std::uint64_t num_vars = reader.read_u64();
    std::vector<Variable> own_vars;
    own_vars.reserve(static_cast<std::size_t>(num_vars));
    for (std::uint64_t i = 0; i < num_vars; ++i) {
        auto kind = static_cast<VarKind>(reader.read_u8());
        double lower = reader.read_f64();
        double upper = reader.read_f64();
        std::string name = reader.read_string();
        VariableDef def = kind == VarKind::Continuous  ? VariableDef::continuous()
                           : kind == VarKind::Integer   ? VariableDef::integer()
                                                         : VariableDef::binary();
        def.bound(lower, upper);
        own_vars.push_back(block.add_var(std::move(name), def));
    }

    std::uint64_t num_children = reader.read_u64();
    std::vector<std::string> child_names;
    std::vector<double> child_multipliers;
    std::vector<RebuiltBlock> children;
    child_names.reserve(static_cast<std::size_t>(num_children));
    child_multipliers.reserve(static_cast<std::size_t>(num_children));
    children.reserve(static_cast<std::size_t>(num_children));
    for (std::uint64_t i = 0; i < num_children; ++i) {
        child_names.push_back(reader.read_string());
        child_multipliers.push_back(reader.read_f64());
        children.push_back(read_block(reader));
    }

    std::uint64_t num_constraints = reader.read_u64();
    for (std::uint64_t i = 0; i < num_constraints; ++i) {
        std::string name = reader.read_string();
        double lower = reader.read_f64();
        double upper = reader.read_f64();
        LinearExpression expr = read_expression(reader, own_vars, children);
        block.add_constraint(std::move(name), Constraint(std::move(expr), lower, upper));
    }

    block.set_objective(read_quadratic_expression(reader, own_vars, children));

    std::uint64_t num_sos2 = reader.read_u64();
    for (std::uint64_t i = 0; i < num_sos2; ++i) {
        std::string name = reader.read_string();
        std::uint64_t num_members = reader.read_u64();
        std::vector<Variable> members;
        members.reserve(static_cast<std::size_t>(num_members));
        for (std::uint64_t j = 0; j < num_members; ++j) members.push_back(read_var_ref(reader, own_vars, children));
        block.add_sos2(std::move(name), std::move(members));
    }

    std::uint64_t num_piecewise = reader.read_u64();
    std::vector<std::pair<std::string, LinearExpression>> piecewise;
    for (std::uint64_t i = 0; i < num_piecewise; ++i) {
        std::string name = reader.read_string();
        piecewise.emplace_back(std::move(name), read_expression(reader, own_vars, children));
    }

    for (std::size_t i = 0; i < children.size(); ++i) {
        block.add_block_scaled(child_names[i], std::move(children[i].block), child_multipliers[i]);
    }

    for (auto& [name, value] : piecewise) {
        if (!block.block(name)) throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
        detail::BlockReader::restore_piecewise(block, std::move(name), std::move(value));
    }

    // Groups of variables, of constraints, and of child blocks.
    for (int kind = 0; kind < 3; ++kind) {
        std::uint64_t num_groups = reader.read_u64();
        for (std::uint64_t g = 0; g < num_groups; ++g) {
            std::string name = reader.read_string();
            std::uint64_t num_members = reader.read_u64();
            std::vector<std::uint64_t> members;
            for (std::uint64_t i = 0; i < num_members; ++i) members.push_back(reader.read_u64());
            std::optional<std::vector<std::pair<KeyValue, std::uint64_t>>> keys;
            if (reader.read_u8() != 0) {
                keys.emplace();
                std::uint64_t num_keys = reader.read_u64();
                for (std::uint64_t i = 0; i < num_keys; ++i) {
                    KeyValue key = read_key(reader);
                    keys->emplace_back(std::move(key), reader.read_u64());
                }
            }
            detail::BlockReader::restore_group(block, kind, std::move(name), members, keys);
        }
    }

    return RebuiltBlock{std::move(block), std::move(own_vars), std::move(children)};
}

}  // namespace

void Block::write_var_ref(std::vector<std::byte>& out, const Block& block, Variable var) {
    std::vector<std::uint64_t> path;
    if (!find_var_path(block, var, path)) {
        throw std::runtime_error(
            "Block::serialize: a constraint or objective references a variable outside "
            "this block's own subtree");
    }
    append_u64(out, path.size());
    for (std::uint64_t p : path) append_u64(out, p);
    append_u64(out, var.index());
}

void Block::write_expression(std::vector<std::byte>& out, const Block& block,
                              const LinearExpression& expr) {
    append_f64(out, expr.constant());
    append_u64(out, expr.terms().size());
    for (const auto& [var, coeff] : expr.terms()) {
        write_var_ref(out, block, var);
        append_f64(out, coeff);
    }
}

void Block::write_quadratic_expression(std::vector<std::byte>& out, const Block& block,
                                       const QuadraticExpression& expr) {
    write_expression(out, block, expr.linear());
    append_u64(out, expr.quadratic_terms().size());
    for (const auto& [pair, coeff] : expr.quadratic_terms()) {
        write_var_ref(out, block, pair.first());
        write_var_ref(out, block, pair.second());
        append_f64(out, coeff);
    }
}

void Block::write_block(std::vector<std::byte>& out, const Block& block) {
    std::vector<std::string> var_names(block.vars_.size());
    for (const auto& [name, var] : block.var_names_) var_names[var.index()] = name;

    append_u64(out, block.vars_.size());
    for (std::size_t i = 0; i < block.vars_.size(); ++i) {
        const VarData& vd = block.vars_[i];
        append_u8(out, static_cast<std::uint8_t>(vd.kind));
        append_f64(out, vd.lower);
        append_f64(out, vd.upper);
        append_string(out, var_names[i]);
    }

    std::vector<std::string> child_names(block.children_.size());
    for (const auto& [name, index] : block.child_names_) child_names[index] = name;
    append_u64(out, block.children_.size());
    for (std::size_t i = 0; i < block.children_.size(); ++i) {
        append_string(out, child_names[i]);
        append_f64(out, block.children_[i].multiplier);
        write_block(out, block.children_[i].block);
    }

    std::vector<std::string> constraint_names(block.constraints_.size());
    for (const auto& [name, ref] : block.constraint_names_) constraint_names[ref.index()] = name;
    append_u64(out, block.constraints_.size());
    for (std::size_t i = 0; i < block.constraints_.size(); ++i) {
        const Constraint& c = block.constraints_[i].constraint;
        append_string(out, constraint_names[i]);
        append_f64(out, c.lower());
        append_f64(out, c.upper());
        write_expression(out, block, c.expression());
    }

    write_quadratic_expression(out, block, block.objective_);

    std::vector<std::string> sos2_names(block.sos2_.size());
    for (const auto& [name, index] : block.sos2_names_) sos2_names[index] = name;
    append_u64(out, block.sos2_.size());
    for (std::size_t i = 0; i < block.sos2_.size(); ++i) {
        append_string(out, sos2_names[i]);
        append_u64(out, block.sos2_[i].vars.size());
        for (Variable var : block.sos2_[i].vars) write_var_ref(out, block, var);
    }

    append_u64(out, block.piecewise_.size());
    for (const auto& [name, f] : block.piecewise_) {
        append_string(out, name);
        write_expression(out, block, f.value);
    }

    append_groups(out, block.indexed_var_groups_, [](Variable v) { return std::uint64_t{v.index()}; });
    append_groups(out, block.indexed_constraint_groups_, [](ConstraintRef c) { return std::uint64_t{c.index()}; });
    append_groups(out, block.indexed_block_groups_, [](BlockRef b) { return std::uint64_t{b.index_}; });
}

void Block::restore_group(int kind, std::string name, const std::vector<std::uint64_t>& members,
                          const std::optional<std::vector<std::pair<KeyValue, std::uint64_t>>>& keys) {
    auto restore = [&](auto& groups, std::size_t count, auto make) {
        using V = decltype(make(std::uint64_t{}));
        auto item = [&](std::uint64_t i) {
            if (i >= count) throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
            return make(i);
        };
        IndexedGroup<V> group;
        for (std::uint64_t i : members) group.ordered.push_back(item(i));
        if (keys) {
            KeyedItems<V> items;
            for (const auto& [key, i] : *keys) items.emplace_back(key, item(i));
            group.restored = std::move(items);
        }
        if (!groups.emplace(std::move(name), std::move(group)).second) {
            throw std::runtime_error("Block::deserialize: truncated or malformed buffer");
        }
    };
    switch (kind) {
        case 0:
            restore(indexed_var_groups_, vars_.size(), [&](std::uint64_t i) { return Variable(id_, i); });
            break;
        case 1:
            restore(indexed_constraint_groups_, constraints_.size(),
                    [&](std::uint64_t i) { return ConstraintRef(id_, i); });
            break;
        default:
            restore(indexed_block_groups_, children_.size(), [](std::uint64_t i) { return BlockRef(i); });
            break;
    }
}

std::vector<std::byte> Block::serialize() const {
    std::vector<std::byte> out;
    write_block(out, *this);
    return out;
}

Block Block::deserialize(const std::vector<std::byte>& bytes) {
    Reader reader(bytes);
    return std::move(read_block(reader).block);
}

}  // namespace saiopt
