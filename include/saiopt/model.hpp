#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "saiopt/block.hpp"
#include "saiopt/objective.hpp"

namespace saiopt {

// The primal solution to a solved Model: whether it's optimal, the
// objective value, and the value assigned to every variable across the
// whole Block tree.
struct SolveResult {
    bool optimal = false;
    double objective_value = 0.0;
    std::unordered_map<Variable, double> variable_values;
    std::string status_message;

    double value(Variable var) const { return variable_values.at(var); }

    // Look up the solution value for every variable in an Indexed<K,
    // Variable>, returning them in the same shape.
    template <typename K>
    Indexed<K, double> values(const Indexed<K, Variable>& indexed) const {
        std::map<K, double> map;
        for (const auto& [k, v] : indexed) {
            map.emplace(k, value(v));
        }
        return Indexed<K, double>::from_map(std::move(map));
    }
};

// A mathematical optimization model: the root Block of a tree of
// variables, linear constraints, and objective contributions, built up
// with ordinary C++ arithmetic and then solved via HiGHS. Model is the
// only place a Block tree can be solved from.
class Model {
public:
    Model() = default;

    Variable add_var(VariableDef def) { return root_.add_var(std::move(def)); }

    template <typename Range, typename DefFn>
    auto add_vars(const Range& index, DefFn&& def) {
        return root_.add_vars(index, std::forward<DefFn>(def));
    }

    ConstraintRef add_constraint(std::string name, Constraint constraint) {
        return root_.add_constraint(std::move(name), std::move(constraint));
    }

    template <typename Range, typename Fn>
    auto add_constraints(std::string name, const Range& index, Fn&& f) {
        return root_.add_constraints(std::move(name), index, std::forward<Fn>(f));
    }

    // Move `child` with a multiplier of 1.0, under `name` so it can later
    // be reached with child/child_mut. See add_block_scaled.
    BlockRef add_block(std::string name, Block child) {
        return root_.add_block(std::move(name), std::move(child));
    }

    // Move `child` into this model's tree under `name`; `child`'s own
    // objective is added to the model's total objective, scaled by
    // `multiplier`.
    BlockRef add_block_scaled(std::string name, Block child, double multiplier) {
        return root_.add_block_scaled(std::move(name), std::move(child), multiplier);
    }

    template <typename Range, typename Fn>
    auto add_blocks(std::string name, const Range& index, Fn&& f) {
        return root_.add_blocks(std::move(name), index, std::forward<Fn>(f));
    }

    template <typename Range, typename Fn, typename MultFn>
    auto add_blocks_scaled(std::string name, const Range& index, Fn&& f, MultFn&& multiplier) {
        return root_.add_blocks_scaled(std::move(name), index, std::forward<Fn>(f),
                                        std::forward<MultFn>(multiplier));
    }

    const Block& block(BlockRef ref) const { return root_.block(ref); }
    Block& block(BlockRef ref) { return root_.block(ref); }

    // Look up a variable added with .name(name) at the model's root (not
    // inside any attached block).
    std::optional<Variable> var(const std::string& name) const { return root_.var(name); }

    // Look up a constraint added under `name` at the model's root (not
    // inside any attached block).
    std::optional<ConstraintRef> constraint(const std::string& name) const {
        return root_.constraint(name);
    }

    // Look up a child block added with add_block / add_block_scaled under
    // `name`.
    const Block* child(const std::string& name) const { return root_.child(name); }
    Block* child(const std::string& name) { return root_.child(name); }

    void set_objective(ObjectiveSense sense, LinearExpression expr) {
        sense_ = sense;
        root_.set_objective(std::move(expr));
    }

    // Effective (flattened) objective coefficient of `var` -- what the
    // solver actually sees. See Block::objective_coefficient.
    double objective_coefficient(Variable var) const { return root_.objective_coefficient(var); }

    // Set the effective objective coefficient of `var` by adjusting the
    // local coefficient in the block that owns it, dividing out that
    // block's cumulative multiplier so the *effective* value comes out to
    // `coeff`. If another ancestor block also references `var` in its own
    // objective, that contribution is left untouched -- use block_mut to
    // edit a specific block's term directly.
    void set_objective_coefficient(Variable var, double coeff);

    // Local objective coefficient of `var` in the block that owns it,
    // unscaled by any multiplier on the path to the root.
    double objective_coefficient_local(Variable var) const;

    // Set the local objective coefficient of `var` in the block that owns
    // it, unscaled.
    void set_objective_coefficient_local(Variable var, double coeff);

    double constraint_coefficient(ConstraintRef cref, Variable var) const {
        return root_.constraint_coefficient(cref, var);
    }
    void set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) {
        root_.set_constraint_coefficient(cref, var, coeff);
    }

    double constraint_lower(ConstraintRef cref) const { return root_.constraint_lower(cref); }
    void set_constraint_lower(ConstraintRef cref, double lower) {
        root_.set_constraint_lower(cref, lower);
    }

    double constraint_upper(ConstraintRef cref) const { return root_.constraint_upper(cref); }
    void set_constraint_upper(ConstraintRef cref, double upper) {
        root_.set_constraint_upper(cref, upper);
    }

    SolveResult solve() const;

private:
    friend class ModelFlattener;

    Block root_;
    std::optional<ObjectiveSense> sense_;
};

}  // namespace saiopt
