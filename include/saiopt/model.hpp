#pragma once

#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

// How a solve() ended. Each backend maps its own native status onto
// these; one that has no counterpart for a given value (e.g. HiGHS has
// no gap-limit stop -- reaching mip_rel_gap counts as Optimal there)
// simply never reports it.
enum class SolveStatus {
    Optimal,
    Infeasible,
    Unbounded,
    // Proven to be one or the other, without determining which.
    InfeasibleOrUnbounded,
    TimeLimit,
    // A simplex/IPM iteration limit.
    IterationLimit,
    // A branch-and-bound node limit.
    NodeLimit,
    // A limit on the number of (improving) solutions found.
    SolutionLimit,
    // A MIP gap limit, for a backend that reports reaching one
    // separately from Optimal.
    GapLimit,
    // An objective bound or target value was reached.
    ObjectiveLimit,
    MemoryLimit,
    // Stopped by a progress callback returning false, or by a signal.
    Interrupted,
    // The backend failed (on loading, presolve, solve, or postsolve).
    Error,
    // Anything else, including a status the backend couldn't determine.
    Unknown,
};

std::string_view to_string(SolveStatus status) noexcept;
std::ostream& operator<<(std::ostream& os, SolveStatus status);

// The primal solution to a solved Model: how the solve ended, the
// objective value, and the value assigned to every variable across the
// whole Block tree. Also carries the LP sensitivity information HiGHS
// computes alongside the solution -- duals, reduced costs, and ranging --
// which only exist for an LP basis: if the model has any integer/binary
// variable, these maps are left empty rather than published with the
// zeroed-out values HiGHS returns for a MILP.
struct SolveResult {
    SolveStatus status = SolveStatus::Unknown;

    // Whether the backend returned a feasible primal solution -- always
    // true when status is Optimal, and possibly true when the solve
    // stopped early (e.g. a MIP that hit TimeLimit with an incumbent).
    // objective_value and variable_values are only populated when it is;
    // otherwise objective_value is 0 and variable_values is empty.
    bool has_solution = false;
    double objective_value = 0.0;
    std::unordered_map<Variable, double> variable_values;

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

    bool optimal() const noexcept { return status == SolveStatus::Optimal; }

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
//
// Privately inherits Block rather than composing one: a Model *is*, for
// every purpose but the ones below, simply the root Block of its own
// tree -- add_var/add_constraint/add_block[_scaled]/block/var/constraint
// (and their indexed and index-free counterparts) all mean exactly the
// same thing here as they do on any other Block, just anchored at the
// root, so bringing them in with `using` needs no re-explaining of their
// contract. It's *private* inheritance, not public: a Model carries one
// thing no other Block does (`sense_`, its solve-time objective
// direction), and Block is passed by value all over its own API
// (add_block/add_block_scaled/ChildBlock) -- if Model were-a Block
// publicly, passing one to any of those would silently slice `sense_`
// off with no warning. Private inheritance gets the shared behavior back
// without ever letting a Model implicitly become a plain Block.
//
// Block's own set_objective(expr) (only ever a local, unscaled
// *contribution* -- meaningful for a non-root Block, scaled by whatever
// multiplier it's attached with) is deliberately NOT brought in the same
// way: only *this* class's own two-argument set_objective(sense, expr)
// below is public, since a Model with a contribution but no sense would
// otherwise fail to solve for a reason that's easy to miss.
class Model : private Block {
public:
    using Block::add_block;
    using Block::add_block_scaled;
    using Block::add_blocks;
    using Block::add_blocks_scaled;
    using Block::add_constraint;
    using Block::add_constraints;
    using Block::add_var;
    using Block::add_vars;
    using Block::block;
    using Block::blocks;
    using Block::constraint;
    using Block::constraints;
    using Block::var;
    using Block::vars;

    Model() = default;

    // Promotes a standalone Block into a Model, as its root -- e.g. a
    // Block built by a reusable function that may end up either attached
    // as some other Model's child, or solved directly as its own Model,
    // depending on the caller (see to_block() below for the reverse).
    // `block`'s own local objective, if it set one, becomes the promoted
    // Model's own root contribution, exactly as it would for any other
    // already-Model-rooted Block. sense_ is left unset here, so a solve
    // still needs set_objective(sense, ...) first -- which replaces
    // `block`'s own objective, the same as calling it a second time on
    // any Model always does; use the two-argument constructor below to
    // attach a sense without touching an objective already set.
    //
    // Explicit: a Block shouldn't silently become a Model wherever one's
    // expected (e.g. passed to a function taking `const Model&`).
    explicit Model(Block block) : Block(std::move(block)) {}

    // Same promotion, but also fixing the solve-time objective sense up
    // front, keeping `block`'s own already-set local objective as-is --
    // for a Block a reusable builder function already gave a complete
    // objective, needing only a sense to become solvable on its own.
    Model(Block block, ObjectiveSense sense) : Block(std::move(block)), sense_(sense) {}

    // The reverse of the two constructors above: hands back this Model's
    // own content as a plain Block -- e.g. to attach it as a child block
    // somewhere else instead of solving it directly -- discarding sense_
    // (a Block has no notion of solve direction; only a model root does).
    // Rvalue-qualified: consumes *this, the same way the rest of Block's
    // own API always transfers ownership of one rather than copying it.
    Block to_block() && { return std::move(static_cast<Block&>(*this)); }

    void set_objective(ObjectiveSense sense, LinearExpression expr) {
        sense_ = sense;
        Block::set_objective(std::move(expr));
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
    // name, qualified by its block's full path (e.g. "sub.y" for a name
    // set inside a block added under "sub") -- every variable has one
    // (add_var requires it), so the "#<index>" fallback only shows up for
    // a variable this model's own tree doesn't actually own (a stray
    // out-of-subtree reference flatten() would otherwise reject at solve
    // time; print() doesn't validate that, so it can't crash on one).
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

    std::optional<ObjectiveSense> sense_;
};

}  // namespace saiopt
