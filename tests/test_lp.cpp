#include <cmath>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("solves a simple minimization LP", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());
    Variable y = model.add_var(VariableDef::continuous().bound(0.0, 6.0));

    model.set_objective(ObjectiveSense::Minimize, x + y);
    model.add_constraint("min_total", x + y >= 10.0);
    model.add_constraint("x_cap", x <= 6.0);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(10.0));
}

TEST_CASE("solves a maximization LP with an equality constraint", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, 20.0));
    Variable y = model.add_var(VariableDef::continuous().bound(0.0, 20.0));

    model.set_objective(ObjectiveSense::Maximize, 3.0 * x + 2.0 * y);
    model.add_constraint("total", x + y == 10.0);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(30.0));
    REQUIRE(result.value(x) == Approx(10.0));
    REQUIRE(result.value(y) == Approx(0.0));
}

TEST_CASE("range constraint bounds both sides of an expression", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());
    Variable y = model.add_var(VariableDef::continuous());

    model.set_objective(ObjectiveSense::Maximize, x + y);
    ConstraintRef total = model.add_constraint("total", 10.0 <= x + y <= 20.0);
    model.add_constraint("x_cap", x <= 20.0);
    model.add_constraint("y_cap", y <= 20.0);

    auto solver = create_solver(model, "highs");
    REQUIRE(solver->constraint_lower(total) == Approx(10.0));
    REQUIRE(solver->constraint_upper(total) == Approx(20.0));

    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(20.0));
}

TEST_CASE("range constraint via >= chaining and constant folding", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());

    model.set_objective(ObjectiveSense::Minimize, x);
    // 15.0 >= (x + 5.0) >= 5.0  =>  0.0 <= x <= 10.0
    ConstraintRef c = model.add_constraint("bounded", 15.0 >= x + 5.0 >= 5.0);

    auto solver = create_solver(model, "highs");
    REQUIRE(solver->constraint_lower(c) == Approx(0.0));
    REQUIRE(solver->constraint_upper(c) == Approx(10.0));
}

TEST_CASE("duals, reduced costs, and sensitivity ranging on a simple LP", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, kInfinity));
    Variable y = model.add_var(VariableDef::continuous().bound(0.0, kInfinity));

    model.set_objective(ObjectiveSense::Minimize, 2.0 * x + 3.0 * y);
    ConstraintRef total = model.add_constraint("total", x + y >= 10.0);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();
    REQUIRE(result.optimal);
    REQUIRE(result.value(x) == Approx(10.0));
    REQUIRE(result.value(y) == Approx(0.0));

    // The binding constraint's shadow price: relaxing it by one unit lets
    // x (the cheaper variable) absorb the slack, raising the objective by
    // its coefficient.
    REQUIRE(result.dual(total) == Approx(2.0));

    // x is basic (nonzero) at the optimum, so its reduced cost is 0; y is
    // pinned to its lower bound, and costs 1 more per unit than routing
    // that unit through x via the constraint (3 - 2*1).
    REQUIRE(result.reduced_cost(x) == Approx(0.0));
    REQUIRE(result.reduced_cost(y) == Approx(1.0));

    // x's cost can range up to 3 (where it ties with y) before the basis
    // would switch; y's cost can drop to 2 for the same reason, with no
    // upper limit since y is already at 0.
    SensitivityRange x_cost = result.objective_coefficient_range(x);
    REQUIRE(x_cost.lower == Approx(0.0));
    REQUIRE(x_cost.upper == Approx(3.0));

    SensitivityRange y_cost = result.objective_coefficient_range(y);
    REQUIRE(y_cost.lower == Approx(2.0));
    REQUIRE(std::isinf(y_cost.upper));
}

TEST_CASE("respects integrality on integer variables", "[milp]") {
    Model model;
    Variable x = model.add_var(VariableDef::integer());

    model.set_objective(ObjectiveSense::Minimize, -1.0 * x);
    model.add_constraint("cap", x <= 3.7);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.value(x) == Approx(3.0));

    // HiGHS zeroes out duals/reduced costs for a MILP rather than giving
    // the final LP relaxation's values, and Highs_getRanging fails
    // outright -- leave these unpublished rather than surface the zeros
    // as if they meant something.
    REQUIRE(result.reduced_costs.empty());
    REQUIRE(result.duals.empty());
    REQUIRE(result.objective_coefficient_ranges.empty());
    REQUIRE(result.variable_bound_ranges.empty());
    REQUIRE(result.constraint_bound_ranges.empty());
}

TEST_CASE("create_solver applies backend-specific options", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, kInfinity));

    model.set_objective(ObjectiveSense::Minimize, x);
    model.add_constraint("cap", x >= 5.0);

    SECTION("a recognized HiGHS option is applied and solving still succeeds") {
        auto solver = create_solver(
            model, "highs", SolverOptions{}.set_double("mip_rel_gap", 0.01).set_bool("output_flag", false));
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
        REQUIRE(result.value(x) == Approx(5.0));
    }

    SECTION("an unrecognized option name throws") {
        REQUIRE_THROWS_AS(
            create_solver(model, "highs", SolverOptions{}.set_bool("not_a_real_option", true)),
            std::runtime_error);
    }

    SECTION("an invalid value for a recognized option throws") {
        REQUIRE_THROWS_AS(create_solver(model, "highs", SolverOptions{}.set_double("mip_rel_gap", -5.0)),
                           std::runtime_error);
    }
}
