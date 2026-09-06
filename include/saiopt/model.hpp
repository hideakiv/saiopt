#pragma once

#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "saiopt/block.hpp"
#include "saiopt/objective.hpp"

namespace saiopt {

// A closed sensitivity range: how far a cost coefficient or a bound can
// move, holding everything else fixed, while the solved basis stays
// optimal.
struct SensitivityRange {
    double lower = 0.0;
    double upper = 0.0;
};

// The primal solution to a solved Model: whether it's optimal, the
// objective value, and the value assigned to every variable across the
// whole Block tree. Also carries the LP sensitivity information HiGHS
// computes alongside the solution -- duals, reduced costs, and ranging --
// which only exist for an LP basis: if the model has any integer/binary
// variable, these maps are left empty rather than published with the
// zeroed-out values HiGHS returns for a MILP.
struct SolveResult {
    bool optimal = false;
    double objective_value = 0.0;
    std::unordered_map<Variable, double> variable_values;
    std::string status_message;

    // Reduced cost of each variable: how much the objective would change
    // per unit increase in that variable's bound, at the margin.
    std::unordered_map<Variable, double> reduced_costs;

    // Dual (shadow price) of each constraint: how much the objective would
    // change per unit relaxation of that constraint's binding bound.
    std::unordered_map<ConstraintRef, double> duals;

    // Range each variable's objective coefficient can move within while
    // the solved basis stays optimal.
    std::unordered_map<Variable, SensitivityRange> objective_coefficient_ranges;

    // Range each variable's own bound can move within while the solved
    // basis stays optimal.
    std::unordered_map<Variable, SensitivityRange> variable_bound_ranges;

    // Range each constraint's binding bound can move within while the
    // solved basis stays optimal.
    std::unordered_map<ConstraintRef, SensitivityRange> constraint_bound_ranges;

    double value(Variable var) const { return variable_values.at(var); }
    double reduced_cost(Variable var) const { return reduced_costs.at(var); }
    double dual(ConstraintRef cref) const { return duals.at(cref); }

    const SensitivityRange& objective_coefficient_range(Variable var) const {
        return objective_coefficient_ranges.at(var);
    }
    const SensitivityRange& variable_bound_range(Variable var) const {
        return variable_bound_ranges.at(var);
    }
    const SensitivityRange& constraint_bound_range(ConstraintRef cref) const {
        return constraint_bound_ranges.at(cref);
    }

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
// with ordinary C++ arithmetic. Model is the only place a Block tree can
// be solved from -- via create_solver(model, solver_name) (see
// saiopt/solver.hpp) -- and carries no dependency on any particular
// solver.
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

    // Print a human-readable summary of the model to `os`: its objective
    // sense, and every block's own objective contribution, variables, and
    // constraints, with child blocks indented under their parent. A
    // variable's label is its own name if it has one (qualified by its
    // block's path, e.g. "sub.y" for a name set inside a block added under
    // "sub"), or "#<index>" if unnamed.
    void print(std::ostream& os) const;

    // Convenience overload, printing to std::cout.
    void print() const;

private:
    friend class ModelFlattener;

    static void collect_labels(const Block& block, const std::string& prefix,
                                std::unordered_map<Variable, std::string>& labels);
    static void print_block(std::ostream& os, const Block& block,
                             const std::unordered_map<Variable, std::string>& labels, int depth);

    Block root_;
    std::optional<ObjectiveSense> sense_;
};

}  // namespace saiopt
