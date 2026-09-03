#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "saiopt/block_id.hpp"
#include "saiopt/constraint.hpp"
#include "saiopt/constraint_ref.hpp"
#include "saiopt/expression.hpp"
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
// Every variable, constraint, and attached block is named (a variable's
// name is optional, via VariableDef::name; a constraint's or attached
// block's name is mandatory) and can be looked back up later with var /
// constraint / child -- each block has its own local namespace, so a lookup
// only ever searches that block's own vars/constraints/children, not its
// descendants'.
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

    Variable add_var(VariableDef def);

    ConstraintRef add_constraint(std::string name, Constraint constraint);

    // Set this block's own local objective contribution. Only meaningful
    // once the block is (transitively) attached to a Model, which combines
    // it with every other attached block's contribution, each scaled by
    // the multiplier it was attached with.
    void set_objective(LinearExpression expr);

    // Move `child` into this block's tree with a multiplier of 1.0, under
    // `name` so it can later be reached with child/child_mut. See
    // add_block_scaled.
    BlockRef add_block(std::string name, Block child);

    // Move `child` into this block's tree under `name`; `child`'s own
    // objective is added to this block's total, scaled by `multiplier`.
    BlockRef add_block_scaled(std::string name, Block child, double multiplier);

    const Block& block(BlockRef ref) const;
    Block& block(BlockRef ref);

    // Look up a variable added with .name(name) in *this* block (not its
    // descendants).
    std::optional<Variable> var(const std::string& name) const;

    // Look up a constraint added under `name` in *this* block (not its
    // descendants).
    std::optional<ConstraintRef> constraint(const std::string& name) const;

    // Look up a child added with add_block / add_block_scaled under `name`.
    const Block* child(const std::string& name) const;
    Block* child(const std::string& name);

    // Local objective coefficient of `var` in this block's own objective
    // expression only -- unscaled by any multiplier on the path to the
    // model root. See objective_coefficient for the effective, flattened
    // value the solver actually sees.
    double objective_coefficient_local(Variable var) const;

    // Set the local objective coefficient of `var` in this block's own
    // objective expression, unscaled.
    void set_objective_coefficient_local(Variable var, double coeff);

    // Effective objective coefficient of `var`: the sum, over this block
    // and every attached descendant whose own objective references `var`,
    // of that block's local coefficient times the product of multipliers
    // from this block down to it. This is what the solver sees.
    double objective_coefficient(Variable var) const;

    // Coefficient of `var` in the constraint referenced by `cref`, wherever
    // in this block's own subtree that constraint was added.
    double constraint_coefficient(ConstraintRef cref, Variable var) const;

    // Set the coefficient of `var` in the constraint referenced by `cref`.
    void set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff);

    // Right-hand side of the constraint referenced by `cref`.
    double constraint_rhs(ConstraintRef cref) const;

    // Set the right-hand side of the constraint referenced by `cref`.
    void set_constraint_rhs(ConstraintRef cref, double rhs);

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

    // Find the block with the given id anywhere in this block's own
    // subtree, including itself.
    const Block* find(BlockId id) const;
    Block* find(BlockId id);

    // Product of multipliers from this block down to the descendant (or
    // self) with the given id, if it's found in this block's own subtree.
    std::optional<double> find_scale(BlockId id, double scale) const;

    double objective_coefficient_scaled(double scale, Variable var) const;

    BlockId id_;
    std::vector<VarData> vars_;
    std::vector<ConstraintData> constraints_;
    LinearExpression objective_;
    std::vector<ChildBlock> children_;
    std::unordered_map<std::string, Variable> var_names_;
    std::unordered_map<std::string, ConstraintRef> constraint_names_;
    std::unordered_map<std::string, std::size_t> child_names_;
};

struct ChildBlock {
    double multiplier;
    Block block;
};

}  // namespace saiopt
