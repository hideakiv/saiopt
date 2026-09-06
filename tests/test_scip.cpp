#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// The same Solver contract test_lp.cpp/test_lp-style tests exercise
// against "highs" ought to hold against "scip" too -- this file spot-
// checks that, plus SCIP-specific behavior worth pinning down: unlike
// HighsSolver, an interrupted ScipSolver resolves cleanly on the same
// instance afterward (see set_progress_callback's doc comment).

using namespace saiopt;
using Catch::Approx;

TEST_CASE("solves a simple minimization LP via scip", "[scip]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());
    Variable y = model.add_var(VariableDef::continuous().bound(0.0, 6.0));

    model.set_objective(ObjectiveSense::Minimize, x + y);
    model.add_constraint("min_total", x + y >= 10.0);
    model.add_constraint("x_cap", x <= 6.0);

    auto solver = create_solver(model, "scip");
    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(10.0));
}

TEST_CASE("respects integrality on integer variables via scip", "[scip][milp]") {
    Model model;
    Variable x = model.add_var(VariableDef::integer());

    model.set_objective(ObjectiveSense::Minimize, -1.0 * x);
    model.add_constraint("cap", x <= 3.7);

    auto solver = create_solver(model, "scip");
    SolveResult result = solver->solve();

    REQUIRE(result.optimal);
    REQUIRE(result.value(x) == Approx(3.0));
}

TEST_CASE("scip solver mutation methods take effect without re-flattening", "[scip]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, 6.0));
    Variable y = model.add_var(VariableDef::continuous().bound(0.0, kInfinity));
    model.set_objective(ObjectiveSense::Minimize, 2.0 * x + 3.0 * y);
    ConstraintRef c = model.add_constraint("c1", x + y >= 10.0);

    auto solver = create_solver(model, "scip");
    REQUIRE(solver->objective_coefficient(x) == Approx(2.0));
    REQUIRE(solver->constraint_coefficient(c, x) == Approx(1.0));
    REQUIRE(solver->constraint_lower(c) == Approx(10.0));
    REQUIRE(solver->variable_upper(x) == Approx(6.0));

    SolveResult r1 = solver->solve();
    REQUIRE(r1.optimal);
    REQUIRE(r1.value(x) == Approx(6.0));  // x maxed out at its cap, cheaper than y
    REQUIRE(r1.value(y) == Approx(4.0));

    solver->set_objective_coefficient(x, 5.0);
    solver->set_objective_coefficient(y, 1.0);
    SolveResult r2 = solver->solve();
    REQUIRE(r2.optimal);
    REQUIRE(r2.value(y) == Approx(10.0));  // y now cheaper
    REQUIRE(r2.value(x) == Approx(0.0));

    solver->set_variable_upper(y, 4.0);
    SolveResult r3 = solver->solve();
    REQUIRE(r3.optimal);
    REQUIRE(r3.value(y) == Approx(4.0));  // y capped, x fills the rest
    REQUIRE(r3.value(x) == Approx(6.0));

    solver->set_variable_kind(x, VarKind::Integer);
    REQUIRE(solver->variable_kind(x) == VarKind::Integer);
    SolveResult r4 = solver->solve();
    REQUIRE(r4.optimal);
}

TEST_CASE("create_solver applies SCIP-specific options", "[scip]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, kInfinity));
    model.set_objective(ObjectiveSense::Minimize, x);
    model.add_constraint("cap", x >= 5.0);

    SECTION("recognized SCIP options are applied and solving still succeeds") {
        auto solver = create_solver(
            model, "scip", SolverOptions{}.set_double("limits/gap", 0.01).set_bool("display/lpinfo", false));
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
        REQUIRE(result.value(x) == Approx(5.0));
    }

    SECTION("an unrecognized option name throws") {
        REQUIRE_THROWS_AS(create_solver(model, "scip", SolverOptions{}.set_bool("not_a_real_option", true)),
                           std::runtime_error);
    }

    SECTION("an int option is tried against SCIP's Longint parameters too") {
        // "limits/nodes" is a Longint-valued SCIP parameter; SolverOptions
        // has no separate Longint slot, so set_int must still work for it.
        auto solver = create_solver(model, "scip", SolverOptions{}.set_int("limits/nodes", 100));
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
    }
}

TEST_CASE("scip progress callback fires and can interrupt, then resolves cleanly", "[scip]") {
    // A multi-dimensional knapsack: a single-constraint knapsack this
    // size turned out to be solved entirely by SCIP's own specialized
    // presolving while developing this backend, before the search tree
    // (and this event handler) ever got a chance to run.
    const int n = 80;
    Model model;
    std::vector<int> items(n);
    for (int i = 0; i < n; ++i) items[i] = i;
    SAIOPT_VARS(model, x, items, .kind = VarKind::Binary);

    LinearExpression value;
    for (int i : items) value += (1.0 + (i * 53 % 29)) * x[i];
    model.set_objective(ObjectiveSense::Maximize, value);

    for (int k = 0; k < 5; ++k) {
        LinearExpression weight;
        for (int i : items) weight += (1.0 + ((i * 37 + k * 17) % 23)) * x[i];
        model.add_constraint("cap" + std::to_string(k), weight <= 130.0 + k * 11);
    }

    int calls = 0;
    auto solver = create_solver(model, "scip");
    solver->set_progress_callback([&](const SolverProgress&) {
        ++calls;
        return true;
    });
    SolveResult full = solver->solve();
    REQUIRE(full.optimal);
    REQUIRE(calls > 0);

    int interrupted_calls = 0;
    auto interrupted_solver = create_solver(model, "scip");
    interrupted_solver->set_progress_callback([&](const SolverProgress&) {
        ++interrupted_calls;
        return false;
    });
    SolveResult interrupted = interrupted_solver->solve();
    REQUIRE_FALSE(interrupted.optimal);
    REQUIRE(interrupted_calls > 0);

    // Unlike HighsSolver, re-solving the SAME (interrupted) instance
    // works cleanly and reaches the true optimum.
    interrupted_solver->set_progress_callback({});
    SolveResult resolved = interrupted_solver->solve();
    REQUIRE(resolved.optimal);
    REQUIRE(resolved.objective_value == Approx(full.objective_value));
}
