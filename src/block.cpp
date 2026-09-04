#include "saiopt/block.hpp"

#include <stdexcept>

namespace saiopt {

Variable Block::add_var(VariableDef def) {
    std::size_t index = vars_.size();
    Variable variable(id_, index);
    if (def.name_) {
        auto [it, inserted] = var_names_.emplace(*def.name_, variable);
        (void)it;
        if (!inserted) {
            throw std::runtime_error("a variable named \"" + *def.name_ +
                                      "\" already exists in this block");
        }
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

void Block::set_objective(LinearExpression expr) { objective_ = std::move(expr); }

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

std::optional<ConstraintRef> Block::constraint(const std::string& name) const {
    auto it = constraint_names_.find(name);
    if (it == constraint_names_.end()) return std::nullopt;
    return it->second;
}

const Block* Block::child(const std::string& name) const {
    auto it = child_names_.find(name);
    if (it == child_names_.end()) return nullptr;
    return &children_[it->second].block;
}

Block* Block::child(const std::string& name) {
    auto it = child_names_.find(name);
    if (it == child_names_.end()) return nullptr;
    return &children_[it->second].block;
}

const Block* Block::find(BlockId id) const {
    if (id_ == id) return this;
    for (const auto& child : children_) {
        if (const Block* found = child.block.find(id)) return found;
    }
    return nullptr;
}

Block* Block::find(BlockId id) {
    return const_cast<Block*>(static_cast<const Block*>(this)->find(id));
}

std::optional<double> Block::find_scale(BlockId id, double scale) const {
    if (id_ == id) return scale;
    for (const auto& child : children_) {
        if (auto found = child.block.find_scale(id, scale * child.multiplier)) return found;
    }
    return std::nullopt;
}

double Block::objective_coefficient_local(Variable var) const { return objective_.coefficient(var); }

void Block::set_objective_coefficient_local(Variable var, double coeff) {
    objective_.set_coefficient(var, coeff);
}

double Block::objective_coefficient(Variable var) const { return objective_coefficient_scaled(1.0, var); }

double Block::objective_coefficient_scaled(double scale, Variable var) const {
    double total = objective_coefficient_local(var) * scale;
    for (const auto& child : children_) {
        total += child.block.objective_coefficient_scaled(scale * child.multiplier, var);
    }
    return total;
}

double Block::constraint_coefficient(ConstraintRef cref, Variable var) const {
    const Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    return owner->constraints_.at(cref.index()).constraint.coefficient(var);
}

void Block::set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) {
    Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    owner->constraints_.at(cref.index()).constraint.set_coefficient(var, coeff);
}

double Block::constraint_lower(ConstraintRef cref) const {
    const Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    return owner->constraints_.at(cref.index()).constraint.lower();
}

void Block::set_constraint_lower(ConstraintRef cref, double lower) {
    Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    owner->constraints_.at(cref.index()).constraint.set_lower(lower);
}

double Block::constraint_upper(ConstraintRef cref) const {
    const Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    return owner->constraints_.at(cref.index()).constraint.upper();
}

void Block::set_constraint_upper(ConstraintRef cref, double upper) {
    Block* owner = find(cref.block());
    if (!owner) throw std::runtime_error("constraint does not belong to this block's subtree");
    owner->constraints_.at(cref.index()).constraint.set_upper(upper);
}

}  // namespace saiopt
