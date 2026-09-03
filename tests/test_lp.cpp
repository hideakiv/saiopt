#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/model.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("solves a simple minimization LP", "[lp]") {
    Model model;
    Variable x = model.add_variable("x");
    Variable y = model.add_variable("y", 0.0, 6.0);

    model.set_objective(x + y, ObjectiveSense::Minimize);
    model.add_constraint(x + y >= 10.0);
    model.add_constraint(x <= 6.0);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(10.0));
}

TEST_CASE("solves a maximization LP with an equality constraint", "[lp]") {
    Model model;
    Variable x = model.add_variable("x", 0.0, 20.0);
    Variable y = model.add_variable("y", 0.0, 20.0);

    model.set_objective(3.0 * x + 2.0 * y, ObjectiveSense::Maximize);
    model.add_constraint(x + y == 10.0);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(30.0));
    REQUIRE(result.variable_values[x.index()] == Approx(10.0));
    REQUIRE(result.variable_values[y.index()] == Approx(0.0));
}

TEST_CASE("respects integrality on integer variables", "[milp]") {
    Model model;
    Variable x = model.add_variable("x", 0.0, kInfinity, VariableType::Integer);

    model.set_objective(-1.0 * x, ObjectiveSense::Minimize);
    model.add_constraint(x <= 3.7);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.variable_values[x.index()] == Approx(3.0));
}
