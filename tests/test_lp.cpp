#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/model.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("solves a simple minimization LP", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());
    Variable y = model.add_var(VariableDef::continuous().upper(6.0));

    model.set_objective(ObjectiveSense::Minimize, x + y);
    model.add_constraint("min_total", x + y >= 10.0);
    model.add_constraint("x_cap", x <= 6.0);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(10.0));
}

TEST_CASE("solves a maximization LP with an equality constraint", "[lp]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().upper(20.0));
    Variable y = model.add_var(VariableDef::continuous().upper(20.0));

    model.set_objective(ObjectiveSense::Maximize, 3.0 * x + 2.0 * y);
    model.add_constraint("total", x + y == 10.0);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(30.0));
    REQUIRE(result.value(x) == Approx(10.0));
    REQUIRE(result.value(y) == Approx(0.0));
}

TEST_CASE("respects integrality on integer variables", "[milp]") {
    Model model;
    Variable x = model.add_var(VariableDef::integer());

    model.set_objective(ObjectiveSense::Minimize, -1.0 * x);
    model.add_constraint("cap", x <= 3.7);

    SolveResult result = model.solve();

    REQUIRE(result.optimal);
    REQUIRE(result.value(x) == Approx(3.0));
}
