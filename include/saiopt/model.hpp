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

    // Look up a child block added with add_block / add_block_scaled under
    // `name`.
    const Block* block(const std::string& name) const { return root_.block(name); }
    Block* block(const std::string& name) { return root_.block(name); }

    // Look up a whole indexed group of child blocks added by add_blocks
    // (or SAIOPT_BLOCKS/add_blocks_scaled/SAIOPT_BLOCKS_SCALED) at the
    // model's root -- see Block::blocks.
    template <typename Range>
    auto blocks(const std::string& name, const Range& index) const {
        return root_.blocks(name, index);
    }
    template <typename Range>
    auto blocks(const std::string& name, const Range& index) {
        return root_.blocks(name, index);
    }

    // Look up a variable added with .name(name) at the model's root (not
    // inside any attached block).
    std::optional<Variable> var(const std::string& name) const { return root_.var(name); }

    // Look up a whole indexed group of variables added by add_vars (or
    // SAIOPT_VARS) at the model's root -- see Block::vars.
    template <typename Range>
    auto vars(const std::string& name, const Range& index) const {
        return root_.vars(name, index);
    }

    // Look up a constraint added under `name` at the model's root (not
    // inside any attached block).
    std::optional<ConstraintRef> constraint(const std::string& name) const {
        return root_.constraint(name);
    }

    // Look up a whole indexed group of constraints added by
    // add_constraints (or SAIOPT_CONSTRAINTS) at the model's root -- see
    // Block::constraints.
    template <typename Range>
    auto constraints(const std::string& name, const Range& index) const {
        return root_.constraints(name, index);
    }

    void set_objective(ObjectiveSense sense, LinearExpression expr) {
        sense_ = sense;
        root_.set_objective(std::move(expr));
    }

    // Print a human-readable summary of the model to `os`: its objective
    // sense, the effective total objective (every block's own
    // contribution, folded together and scaled by the multiplier it was
    // attached with -- this is what the solver actually sees, not each
    // block's own unscaled local objective), and every block's variables
    // and constraints, with child blocks indented under their parent.
    //
    // The objective is printed as an indented, parenthesized outline
    // rather than one long inline expression, so it stays legible no
    // matter how many levels deep the nesting goes: a child block's
    // contribution opens with "name.(" (its multiplier first, when that
    // isn't 1, written the same way a coefficient is -- "2 a.(", not
    // "2 * a.("), continuing whatever line was already open rather than
    // starting a new one; its own folded objective follows indented two
    // spaces further; and a closing ")" begins a new line back at the
    // opening line's own indent, which the *next* sibling term (if any)
    // then continues, e.g.
    //
    //   objective:
    //     x + 3 sub1.(
    //       y + 2 a.(
    //         v
    //       ) - b.(
    //         2 v
    //       )
    //     ) + sub2.(
    //       3 y
    //     )
    //
    // for a block "sub1" (multiplier 3) containing its own term "y" plus
    // two children "a" (multiplier 2) and "b" (multiplier -1), and a
    // sibling block "sub2" (multiplier 1, so no numeric prefix) directly
    // under the root. Variables inside a block's parentheses are named
    // relative to that block (just "y", not "sub1.y"), since the
    // "sub1." is already spelled out once, on the opening parenthesis.
    //
    // Elsewhere (variable listings and constraints, which can span
    // several blocks in one expression) a variable's label is its own
    // name if it has one, qualified by its block's full path (e.g.
    // "sub.y" for a name set inside a block added under "sub"), or
    // "#<index>" if unnamed.
    void print(std::ostream& os) const;

    // Convenience overload, printing to std::cout.
    void print() const;

private:
    friend class ModelFlattener;

    static void collect_labels(const Block& block, const std::string& prefix,
                                std::unordered_map<Variable, std::string>& labels);
    static void print_block(std::ostream& os, const Block& block,
                             const std::unordered_map<Variable, std::string>& labels, int depth);

    // The effective total objective contributed by `block` and its whole
    // subtree, as already-indented lines (absolute indentation -- each
    // line is ready to print as-is, already prefixed with `indent`
    // spaces, or more for anything nested deeper): `block`'s own local
    // objective (its variables named relative to `block` itself, via
    // `prefix`) comes first if it has one, followed by one open-paren/
    // close-paren pair per child with a non-empty contribution, each
    // wrapping that child's own lines indented two spaces deeper. A term
    // that doesn't itself open a nested scope shares a line with
    // whatever came before it (the block's own local objective, or the
    // previous child's close paren); one that does always ends the
    // current line at its open paren and begins a new one at its close --
    // see print()'s doc comment for the exact shape. Empty (no local
    // objective, no non-empty children) returns an empty vector rather
    // than a "0" placeholder, so a contributing-nothing child can be told
    // apart from one that isn't and skipped with no stray parentheses.
    // `prefix` is `block`'s own fully qualified path from the model root
    // (e.g. "sub.gc"), used to turn a variable's globally qualified label
    // back into one relative to `block`.
    static std::vector<std::string> format_total_objective_lines(const Block& block,
                                                                   const std::unordered_map<Variable, std::string>& labels,
                                                                   const std::string& prefix, int indent);

    Block root_;
    std::optional<ObjectiveSense> sense_;
};

}  // namespace saiopt
