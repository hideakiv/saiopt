#include <cstdio>
#include <string>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

namespace {
std::string bound_str(double v) {
    if (v >= saiopt::kInfinity) return "inf";
    if (v <= -saiopt::kInfinity) return "-inf";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}
}  // namespace

// Demonstrates Solver's coefficient/bound get/set methods
// (objective_coefficient, constraint_coefficient, constraint_lower/upper,
// variable_lower/upper, variable_kind, and their setters): a Solver holds
// the flattened LP in the backend's own live state, so reading or
// changing one of these acts on that state directly and never
// re-flattens the Block tree -- solve() after a change picks it up on
// the spot.
int main() {
    using namespace saiopt;

    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 6.0});
    SAIOPT_VAR(model, y, .bounds = {0.0, kInfinity});
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * x + 3.0 * y);
    SAIOPT_CONSTRAINT(model, demand, x + y >= 10.0);

    auto solver = create_solver(model, "highs");

    std::printf("flattened LP: cost(x)=%.1f cost(y)=%.1f  demand: %.1f*x + %.1f*y in [%s, %s]\n",
                solver->objective_coefficient(x), solver->objective_coefficient(y),
                solver->constraint_coefficient(demand, x), solver->constraint_coefficient(demand, y),
                bound_str(solver->constraint_lower(demand)).c_str(),
                bound_str(solver->constraint_upper(demand)).c_str());
    std::printf("x in [%s, %s], kind=%s\n\n", bound_str(solver->variable_lower(x)).c_str(),
                bound_str(solver->variable_upper(x)).c_str(),
                solver->variable_kind(x) == VarKind::Continuous ? "continuous" : "integer");

    SolveResult r1 = solver->solve();
    std::printf("1. baseline: cost=%.2f x=%.2f y=%.2f\n", r1.objective_value, r1.value(x), r1.value(y));

    // set_objective_coefficient: reprice so y is cheaper than x.
    solver->set_objective_coefficient(x, 5.0);
    solver->set_objective_coefficient(y, 1.0);
    SolveResult r2 = solver->solve();
    std::printf("2. after repricing (x=5, y=1): cost=%.2f x=%.2f y=%.2f\n", r2.objective_value,
                r2.value(x), r2.value(y));

    // set_variable_upper: cap the now-cheap y, forcing x back into use.
    solver->set_variable_upper(y, 4.0);
    SolveResult r3 = solver->solve();
    std::printf("3. after capping y at 4: cost=%.2f x=%.2f y=%.2f\n", r3.objective_value, r3.value(x),
                r3.value(y));

    // set_variable_lower: force x above what the constraint alone needs
    // (raising its upper bound first to make room above the new floor).
    solver->set_variable_upper(x, 20.0);
    solver->set_variable_lower(x, 8.0);
    SolveResult r4 = solver->solve();
    std::printf("4. after flooring x at 8: cost=%.2f x=%.2f y=%.2f\n", r4.objective_value, r4.value(x),
                r4.value(y));

    // set_constraint_lower: raise the demand this constraint must meet.
    solver->set_constraint_lower(demand, 20.5);
    SolveResult r5 = solver->solve();
    std::printf("5. after raising demand to 20.5: cost=%.2f x=%.2f y=%.2f\n", r5.objective_value,
                r5.value(x), r5.value(y));

    // set_constraint_coefficient: y now counts double toward demand (e.g.
    // a yield or unit-conversion change).
    solver->set_constraint_coefficient(demand, y, 2.0);
    SolveResult r6 = solver->solve();
    std::printf("6. after doubling y's demand coefficient: cost=%.2f x=%.2f y=%.2f\n",
                r6.objective_value, r6.value(x), r6.value(y));

    // set_variable_kind: force x to whole units. The LP wants x=12.5
    // here, so this rounds x up to 13 and lets y relax down to just
    // cover the rest -- also switching this from an LP to a MILP, which
    // is why duals/ranging disappear (see SolveResult's comment on
    // reduced_costs/duals/*_ranges).
    solver->set_variable_kind(x, VarKind::Integer);
    SolveResult r7 = solver->solve();
    std::printf("7. x now integer: cost=%.2f x=%.2f y=%.2f  duals=%s\n", r7.objective_value,
                r7.value(x), r7.value(y), r7.duals.empty() ? "none (MILP)" : "present");

    // Switch back to continuous -- the fractional optimum (and duals) return.
    solver->set_variable_kind(x, VarKind::Continuous);
    SolveResult r8 = solver->solve();
    std::printf("8. x back to continuous: cost=%.2f x=%.2f y=%.2f  duals=%s\n", r8.objective_value,
                r8.value(x), r8.value(y), r8.duals.empty() ? "none" : "present");
    return 0;
}
