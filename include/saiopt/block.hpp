#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "saiopt/block_id.hpp"
#include "saiopt/constraint.hpp"
#include "saiopt/constraint_ref.hpp"
#include "saiopt/expression.hpp"
#include "saiopt/indexed.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

class ModelFlattener;

// A lightweight, copyable handle to a child block added via
// Block::add_block / Block::add_block_scaled, used to reach it again via
// Block::block.
class BlockRef {
public:
    friend bool operator==(const BlockRef&, const BlockRef&) noexcept = default;

private:
    friend class Block;
    explicit BlockRef(std::size_t index) : index_(index) {}
    std::size_t index_;
};

struct ChildBlock;

// A composable sub-model: its own variables, constraints, and a local
// objective contribution, which can itself have further Blocks attached.
//
// Build a Block standalone with add_var / add_constraint / set_objective,
// then move it into a parent with add_block / add_block_scaled. A Model is
// the special case of Block that can be the root of the tree and can
// actually be solved.
//
// A block's own constraints and objective may only reference variables that
// belong to the block itself or to one of its attached descendants -- never
// to an ancestor. This is enforced when the tree is solved.
//
// Every variable, constraint, and attached block is named -- mandatorily,
// a required parameter of add_var/add_constraint/add_block, not an
// optional property of what's being added -- and can be looked back up
// later with var / constraint / block (or their collection counterparts
// vars / constraints / blocks, for a whole SAIOPT_VARS/SAIOPT_CONSTRAINTS/
// SAIOPT_BLOCKS-declared group at once) -- each block has its own local
// namespace, so a lookup only ever searches that block's own
// vars/constraints/children, not its descendants'. This is what
// guarantees a Variable/ConstraintRef/BlockRef stays reachable by name
// even once its own handle (add_var/add_constraint/add_block's return
// value) is out of scope.
//
// A Block is move-only: copying one would duplicate its BlockId, breaking
// the invariant that a BlockId uniquely identifies one block in the tree.
class Block {
public:
    Block() = default;
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = default;
    Block& operator=(Block&&) = default;

    // Add a variable named `name` to this block, described by `def`
    // (kind and bounds; see VariableDef). Every variable is named -- like
    // add_constraint/add_block, `name` is a required parameter here
    // rather than an optional property of `def` -- so a Variable is
    // always reachable again via var(name) even once its own handle
    // (this call's return value) is out of scope. Throws
    // std::runtime_error if a variable named `name` already exists in
    // this block.
    Variable add_var(std::string name, VariableDef def);

    // Add one variable per item in `index`, built by `def(k)`, each named
    // `{name}[{k}]` (see add_constraints for why the group is named) and
    // remembered as a retrievable group under `name` -- see vars(name)/
    // vars<K>(name). Throws std::runtime_error if a group of variables
    // named `name` was already declared in this block.
    template <typename Range, typename DefFn>
    auto add_vars(std::string name, const Range& index, DefFn&& def) {
        using K = std::decay_t<decltype(*std::begin(index))>;
        if (indexed_var_groups_.count(name)) {
            throw std::runtime_error("a group of variables named \"" + name + "\" already exists in this block");
        }
        std::map<K, Variable> map;
        std::vector<Variable> ordered;
        for (const auto& k : index) {
            Variable v = add_var(detail::indexed_name(name, k), def(k));
            map.emplace(k, v);
            ordered.push_back(v);
        }
        Indexed<K, Variable> result = Indexed<K, Variable>::from_map(std::move(map));
        indexed_var_groups_.emplace(std::move(name), IndexedVarGroup{std::move(ordered), std::any(result)});
        return result;
    }

    ConstraintRef add_constraint(std::string name, Constraint constraint);

    // Add one constraint per item in `index`, built by `f(k)`, each named
    // `{name}[{k}]`. `name` disambiguates this whole group from any other
    // add_constraints call in the same block (two calls sharing a prefix
    // and an overlapping index value would otherwise collide, since every
    // constraint name must be unique within a block), and is remembered
    // so the whole group can be retrieved later via constraints(name)/
    // constraints<K>(name). Throws std::runtime_error if a group of
    // constraints named `name` was already declared in this block.
    template <typename Range, typename Fn>
    auto add_constraints(std::string name, const Range& index, Fn&& f) {
        using K = std::decay_t<decltype(*std::begin(index))>;
        if (indexed_constraint_groups_.count(name)) {
            throw std::runtime_error("a group of constraints named \"" + name + "\" already exists in this block");
        }
        std::map<K, ConstraintRef> map;
        std::vector<ConstraintRef> ordered;
        for (const auto& k : index) {
            ConstraintRef ref = add_constraint(detail::indexed_name(name, k), f(k));
            map.emplace(k, ref);
            ordered.push_back(ref);
        }
        Indexed<K, ConstraintRef> result = Indexed<K, ConstraintRef>::from_map(std::move(map));
        indexed_constraint_groups_.emplace(std::move(name), IndexedConstraintGroup{std::move(ordered), std::any(result)});
        return result;
    }

    // Set this block's own local objective contribution. Only meaningful
    // once the block is (transitively) attached to a Model, which combines
    // it with every other attached block's contribution, each scaled by
    // the multiplier it was attached with. May be quadratic (see
    // QuadraticExpression); a LinearExpression or Variable converts.
    void set_objective(QuadraticExpression expr);

    // Move `child` into this block's tree with a multiplier of 1.0, under
    // `name` so it can later be reached with block(name). See
    // add_block_scaled.
    BlockRef add_block(std::string name, Block child);

    // Move `child` into this block's tree under `name`; `child`'s own
    // objective is added to this block's total, scaled by `multiplier`.
    BlockRef add_block_scaled(std::string name, Block child, double multiplier);

    // Add one child per item in `index`, built by `f(k)`, each with a
    // multiplier of 1.0. See add_blocks_scaled.
    template <typename Range, typename Fn>
    auto add_blocks(std::string name, const Range& index, Fn&& f) {
        return add_blocks_scaled(std::move(name), index, std::forward<Fn>(f),
                                  [](const auto&) { return 1.0; });
    }

    // Add one child per item in `index`, built by `f(k)` and scaled by
    // `multiplier(k)`, each named `{name}[{k}]` (see add_constraints for
    // why the group is named, and for why this throws on a repeated
    // `name`). The group is remembered for later retrieval via
    // blocks(name)/blocks<K>(name).
    template <typename Range, typename Fn, typename MultFn>
    auto add_blocks_scaled(std::string name, const Range& index, Fn&& f, MultFn&& multiplier) {
        using K = std::decay_t<decltype(*std::begin(index))>;
        if (indexed_block_groups_.count(name)) {
            throw std::runtime_error("a group of child blocks named \"" + name + "\" already exists in this block");
        }
        std::map<K, BlockRef> map;
        std::vector<BlockRef> ordered;
        for (const auto& k : index) {
            BlockRef ref = add_block_scaled(detail::indexed_name(name, k), f(k), multiplier(k));
            map.emplace(k, ref);
            ordered.push_back(ref);
        }
        Indexed<K, BlockRef> result = Indexed<K, BlockRef>::from_map(std::move(map));
        indexed_block_groups_.emplace(std::move(name), IndexedBlockGroup{std::move(ordered), std::any(result)});
        return result;
    }

    const Block& block(BlockRef ref) const;
    Block& block(BlockRef ref);

    // Look up a child block added with add_block / add_block_scaled under
    // `name`.
    const Block* block(const std::string& name) const;
    Block* block(const std::string& name);

    // Look up a whole indexed group of child blocks added by add_blocks
    // (or SAIOPT_BLOCKS/add_blocks_scaled/SAIOPT_BLOCKS_SCALED) in *this*
    // block, replaying the same "{name}[{key}]" naming convention those
    // used to name each one -- the collection counterpart to the by-name
    // block() above, for getting back every child (as an
    // Indexed<K, const Block*>/Indexed<K, Block*>, not one Block* at a
    // time) once the original Indexed<K, BlockRef> those macros returned
    // is out of scope. `index` need not be the exact same range object
    // add_blocks was called with, only equal key-for-key. See vars() (the
    // same thing, for variables) for the full rationale, including why
    // this throws (naming the first missing key) rather than silently
    // skipping a child that isn't there.
    template <typename Range>
    auto blocks(const std::string& name, const Range& index) const {
        using K = std::decay_t<decltype(*std::begin(index))>;
        std::map<K, const Block*> map;
        for (const auto& k : index) {
            std::string full_name = detail::indexed_name(name, k);
            const Block* b = block(full_name);
            if (!b) throw std::runtime_error("blocks(): no child block named \"" + full_name + "\" in this block");
            map.emplace(k, b);
        }
        return Indexed<K, const Block*>::from_map(std::move(map));
    }

    template <typename Range>
    auto blocks(const std::string& name, const Range& index) {
        using K = std::decay_t<decltype(*std::begin(index))>;
        std::map<K, Block*> map;
        for (const auto& k : index) {
            std::string full_name = detail::indexed_name(name, k);
            Block* b = block(full_name);
            if (!b) throw std::runtime_error("blocks(): no child block named \"" + full_name + "\" in this block");
            map.emplace(k, b);
        }
        return Indexed<K, Block*>::from_map(std::move(map));
    }

    // Every child block declared under `name` (via add_blocks/
    // add_blocks_scaled or their SAIOPT_* macro forms), in declaration
    // order -- the untyped, index-free counterpart to blocks(name,
    // index): no index needed, but the result can't be looked up by key.
    // Throws std::runtime_error if no such group was declared.
    std::vector<const Block*> blocks(const std::string& name) const;
    std::vector<Block*> blocks(const std::string& name);

    // The exact Indexed<K, BlockRef> add_blocks[_scaled](name, index, ...)
    // built when the group named `name` was declared (resolved to
    // Block*/const Block*, same as blocks(name, index)) -- the typed,
    // index-free counterpart: no index needed, but `K` must be supplied
    // explicitly (it can't be deduced from `name` alone), e.g.
    // `block.blocks<std::string>("region")`. Throws std::runtime_error if
    // no such group was declared, or if it was declared with a different
    // key type than `K`.
    template <typename K>
    Indexed<K, const Block*> blocks(const std::string& name) const {
        const IndexedBlockGroup& group = find_block_group(name);
        const auto* typed = std::any_cast<Indexed<K, BlockRef>>(&group.typed);
        if (!typed) {
            throw std::runtime_error("blocks<K>(): the group of child blocks named \"" + name +
                                      "\" wasn't declared with this key type");
        }
        std::map<K, const Block*> resolved;
        for (const auto& [k, ref] : *typed) resolved.emplace(k, &block(ref));
        return Indexed<K, const Block*>::from_map(std::move(resolved));
    }

    template <typename K>
    Indexed<K, Block*> blocks(const std::string& name) {
        const IndexedBlockGroup& group = find_block_group(name);
        const auto* typed = std::any_cast<Indexed<K, BlockRef>>(&group.typed);
        if (!typed) {
            throw std::runtime_error("blocks<K>(): the group of child blocks named \"" + name +
                                      "\" wasn't declared with this key type");
        }
        std::map<K, Block*> resolved;
        for (const auto& [k, ref] : *typed) resolved.emplace(k, &block(ref));
        return Indexed<K, Block*>::from_map(std::move(resolved));
    }

    // Look up a variable added with .name(name) in *this* block (not its
    // descendants).
    std::optional<Variable> var(const std::string& name) const;

    // Look up a whole indexed group of variables added by add_vars (or
    // SAIOPT_VARS) in *this* block, replaying the same "{name}[{key}]"
    // naming convention add_vars used to name each one -- the collection
    // counterpart to var(), for getting back an Indexed<K, Variable> (not
    // one Variable at a time) once the original handle add_vars/SAIOPT_VARS
    // returned is out of scope. `index` need not be the exact same range
    // object add_vars was called with, only equal key-for-key.
    //
    // Unlike var(), which returns an empty std::optional for a name that
    // isn't there, this throws std::runtime_error naming the first missing
    // key: a whole indexed group either was declared with this exact index
    // or wasn't, so a partial result would just be a bug hiding as data.
    template <typename Range>
    auto vars(const std::string& name, const Range& index) const {
        using K = std::decay_t<decltype(*std::begin(index))>;
        std::map<K, Variable> map;
        for (const auto& k : index) {
            std::string full_name = detail::indexed_name(name, k);
            std::optional<Variable> v = var(full_name);
            if (!v) throw std::runtime_error("vars(): no variable named \"" + full_name + "\" in this block");
            map.emplace(k, *v);
        }
        return Indexed<K, Variable>::from_map(std::move(map));
    }

    // Every variable declared under `name` (via the named add_vars
    // overload, or SAIOPT_VARS), in declaration order -- the untyped,
    // index-free counterpart to vars(name, index): no index needed, but
    // the result can't be looked up by key. Throws std::runtime_error if
    // no such group was declared.
    std::vector<Variable> vars(const std::string& name) const;

    // The exact Indexed<K, Variable> add_vars(name, index, def) built
    // when the group named `name` was declared -- the typed, index-free
    // counterpart: no index needed, but `K` must be supplied explicitly
    // (it can't be deduced from `name` alone), e.g.
    // `block.vars<std::string>("open")`. Throws std::runtime_error if no
    // such group was declared, or if it was declared with a different
    // key type than `K`.
    template <typename K>
    Indexed<K, Variable> vars(const std::string& name) const {
        const IndexedVarGroup& group = find_var_group(name);
        const auto* typed = std::any_cast<Indexed<K, Variable>>(&group.typed);
        if (!typed) {
            throw std::runtime_error("vars<K>(): the group of variables named \"" + name +
                                      "\" wasn't declared with this key type");
        }
        return *typed;
    }

    // Look up a constraint added under `name` in *this* block (not its
    // descendants).
    std::optional<ConstraintRef> constraint(const std::string& name) const;

    // Look up a whole indexed group of constraints added by
    // add_constraints (or SAIOPT_CONSTRAINTS) in *this* block, replaying
    // the same "{name}[{key}]" naming convention add_constraints used to
    // name each one -- the collection counterpart to constraint(), for
    // getting back an Indexed<K, ConstraintRef> (not one ConstraintRef at
    // a time) once the original handle add_constraints/SAIOPT_CONSTRAINTS
    // returned is out of scope. `index` need not be the exact same range
    // object add_constraints was called with, only equal key-for-key. See
    // vars() (the same thing, for variables) for the full rationale.
    //
    // Unlike constraint(), which returns an empty std::optional for a
    // name that isn't there, this throws std::runtime_error naming the
    // first missing key: a whole indexed group either was declared with
    // this exact index or wasn't, so a partial result would just be a bug
    // hiding as data.
    template <typename Range>
    auto constraints(const std::string& name, const Range& index) const {
        using K = std::decay_t<decltype(*std::begin(index))>;
        std::map<K, ConstraintRef> map;
        for (const auto& k : index) {
            std::string full_name = detail::indexed_name(name, k);
            std::optional<ConstraintRef> c = constraint(full_name);
            if (!c) {
                throw std::runtime_error("constraints(): no constraint named \"" + full_name + "\" in this block");
            }
            map.emplace(k, *c);
        }
        return Indexed<K, ConstraintRef>::from_map(std::move(map));
    }

    // Every constraint declared under `name` (via the named
    // add_constraints, or SAIOPT_CONSTRAINTS), in declaration order --
    // the untyped, index-free counterpart to constraints(name, index): no
    // index needed, but the result can't be looked up by key. Throws
    // std::runtime_error if no such group was declared.
    std::vector<ConstraintRef> constraints(const std::string& name) const;

    // The exact Indexed<K, ConstraintRef> add_constraints(name, index, f)
    // built when the group named `name` was declared -- the typed,
    // index-free counterpart: no index needed, but `K` must be supplied
    // explicitly (it can't be deduced from `name` alone), e.g.
    // `block.constraints<std::string>("cap")`. Throws std::runtime_error
    // if no such group was declared, or if it was declared with a
    // different key type than `K`.
    template <typename K>
    Indexed<K, ConstraintRef> constraints(const std::string& name) const {
        const IndexedConstraintGroup& group = find_constraint_group(name);
        const auto* typed = std::any_cast<Indexed<K, ConstraintRef>>(&group.typed);
        if (!typed) {
            throw std::runtime_error("constraints<K>(): the group of constraints named \"" + name +
                                      "\" wasn't declared with this key type");
        }
        return *typed;
    }

    // Serialize this block's whole structure -- variables, constraints,
    // objective, and attached children, recursively, all with their names
    // -- to a flat byte buffer. Meant for handing to another process (e.g.
    // over MPI, see saiopt/mpi.hpp) and reconstructing there with
    // Block::deserialize.
    std::vector<std::byte> serialize() const;

    // Reconstruct a Block from a buffer produced by serialize(), which may
    // have come from a different process. Rebuilds via the ordinary public
    // API (add_var/add_constraint/set_objective/add_block_scaled) rather
    // than copying raw state, so the result is assigned fresh, locally-
    // unique BlockIds -- safe even if the sending process's BlockId values
    // happen to collide with ones already in use here. Throws
    // std::runtime_error if `bytes` is truncated or malformed.
    static Block deserialize(const std::vector<std::byte>& bytes);

private:
    friend class Model;
    friend class ModelFlattener;

    struct VarData {
        VarKind kind;
        double lower;
        double upper;
    };

    struct ConstraintData {
        Constraint constraint;
    };

    // One indexed group's worth of state, recorded at add_vars(name, ...)
    // /add_constraints/add_blocks[_scaled] time: `ordered` is every item
    // in declaration order, type-erased down to the value type V (backs
    // the untyped vars(name)/constraints(name)/blocks(name)); `typed` is
    // the exact Indexed<K, V> those calls returned, kept as a std::any
    // since K varies per group and Block itself isn't templated on it
    // (backs the typed vars<K>(name)/constraints<K>(name)/blocks<K>(name)).
    template <typename V>
    struct IndexedGroup {
        std::vector<V> ordered;
        std::any typed;  // Indexed<K, V> for whichever K this group used
    };
    using IndexedVarGroup = IndexedGroup<Variable>;
    using IndexedConstraintGroup = IndexedGroup<ConstraintRef>;
    using IndexedBlockGroup = IndexedGroup<BlockRef>;

    // `group_kind` is only for the error message (e.g. "variables"); the
    // three call sites each already know which of the three maps they
    // mean at compile time.
    template <typename Group>
    static const Group& find_indexed_group(const std::unordered_map<std::string, Group>& groups,
                                            const std::string& name, const char* group_kind) {
        auto it = groups.find(name);
        if (it == groups.end()) {
            throw std::runtime_error("no indexed group of " + std::string(group_kind) + " named \"" + name +
                                      "\" in this block");
        }
        return it->second;
    }
    const IndexedVarGroup& find_var_group(const std::string& name) const {
        return find_indexed_group(indexed_var_groups_, name, "variables");
    }
    const IndexedConstraintGroup& find_constraint_group(const std::string& name) const {
        return find_indexed_group(indexed_constraint_groups_, name, "constraints");
    }
    const IndexedBlockGroup& find_block_group(const std::string& name) const {
        return find_indexed_group(indexed_block_groups_, name, "child blocks");
    }

    // Path (as a sequence of child indices, walked from `block` down) to
    // the descendant (or `block` itself, with an empty path) that owns
    // `var`, appended onto `path`. Returns false, leaving `path`
    // unmodified, if `var` isn't found in `block`'s own subtree.
    static bool find_var_path(const Block& block, Variable var, std::vector<std::uint64_t>& path);

    // Appends `block`'s wire encoding (see serialize()) onto `out`.
    static void write_block(std::vector<std::byte>& out, const Block& block);
    static void write_var_ref(std::vector<std::byte>& out, const Block& block, Variable var);
    static void write_quadratic_expression(std::vector<std::byte>& out, const Block& block,
                                           const QuadraticExpression& expr);
    static void write_expression(std::vector<std::byte>& out, const Block& block,
                                  const LinearExpression& expr);

    BlockId id_;
    std::vector<VarData> vars_;
    std::vector<ConstraintData> constraints_;
    QuadraticExpression objective_;
    std::vector<ChildBlock> children_;
    std::unordered_map<std::string, Variable> var_names_;
    std::unordered_map<std::string, ConstraintRef> constraint_names_;
    std::unordered_map<std::string, std::size_t> child_names_;
    std::unordered_map<std::string, IndexedVarGroup> indexed_var_groups_;
    std::unordered_map<std::string, IndexedConstraintGroup> indexed_constraint_groups_;
    std::unordered_map<std::string, IndexedBlockGroup> indexed_block_groups_;
};

struct ChildBlock {
    double multiplier;
    Block block;
};

}  // namespace saiopt
