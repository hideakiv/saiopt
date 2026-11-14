#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

namespace {

// A multi-constraint knapsack big enough that neither backend solves it
// outright in presolve, plus a continuous `bonus` tied to the packed
// value -- so a start that only fixes the binaries leaves something for
// the backend to complete.
struct Knapsack {
    Model model;
    std::vector<Variable> items;
};

Knapsack build_knapsack() {
    const int n = 80;
    Model model;
    std::vector<int> index(n);
    for (int i = 0; i < n; ++i) index[i] = i;
    SAIOPT_VARS(model, x, index, .kind = VarKind::Binary);
    SAIOPT_VAR(model, bonus, .bounds = {0.0, 1000.0});

    LinearExpression value;
    for (int i : index) value += (1.0 + (i * 53 % 29)) * x[i];
    SAIOPT_CONSTRAINT(model, bonus_cap, bonus <= 0.1 * value);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, value + bonus);

    for (int c = 0; c < 5; ++c) {
        LinearExpression weight;
        for (int i : index) weight += (1.0 + ((i * 37 + c * 17) % 23)) * x[i];
        model.add_constraint("cap" + std::to_string(c), weight <= 130.0 + c * 11);
    }

    std::vector<Variable> items;
    for (int i : index) items.push_back(x[i]);
    return Knapsack{std::move(model), std::move(items)};
}

// Each backend's own option for "stop as soon as one solution is found".
SolverOptions stop_at_first_solution(const std::string& backend) {
    return backend == "highs" ? SolverOptions{}.set_int("mip_max_improving_sols", 1)
                              : SolverOptions{}.set_int("limits/solutions", 1);
}

}  // namespace

TEST_CASE("an initial solution seeds the search with an incumbent", "[initial]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    Knapsack k = build_knapsack();
    SolveResult optimum = create_solver(k.model, backend)->solve();
    REQUIRE(optimum.optimal());

    // Without a start, stopping at the first solution found falls short
    // of the optimum -- otherwise the checks below couldn't tell whether
    // the start was used at all.
    SolveResult unseeded = create_solver(k.model, backend, stop_at_first_solution(backend))->solve();
    REQUIRE(unseeded.has_solution);
    REQUIRE(unseeded.objective_value < optimum.objective_value - 1e-6);

    SECTION("a complete start") {
        auto solver = create_solver(k.model, backend, stop_at_first_solution(backend));
        solver->set_initial_solution(optimum.variable_values);
        SolveResult seeded = solver->solve();
        REQUIRE(seeded.has_solution);
        REQUIRE(seeded.objective_value == Approx(optimum.objective_value));
    }

    SECTION("a partial start, completed by the backend") {
        std::unordered_map<Variable, double> binaries;
        for (Variable v : k.items) binaries.emplace(v, optimum.value(v));

        auto solver = create_solver(k.model, backend, stop_at_first_solution(backend));
        solver->set_initial_solution(binaries);
        SolveResult seeded = solver->solve();
        REQUIRE(seeded.has_solution);
        REQUIRE(seeded.objective_value == Approx(optimum.objective_value));
    }

    SECTION("an infeasible start is dropped, not an error") {
        std::unordered_map<Variable, double> everything;
        for (Variable v : k.items) everything.emplace(v, 1.0);

        auto solver = create_solver(k.model, backend);
        solver->set_initial_solution(everything);
        SolveResult result = solver->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.objective_value == Approx(optimum.objective_value));
    }
}

TEST_CASE("an initial solution doesn't disturb an LP solve", "[initial]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    Model model;
    SAIOPT_VAR(model, x);
    SAIOPT_VAR(model, y, .bounds = {0.0, 6.0});
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x + 2.0 * y);
    SAIOPT_CONSTRAINT(model, total, x + y >= 10.0);
    SAIOPT_CONSTRAINT(model, x_cap, x <= 7.0);

    auto solver = create_solver(model, backend);
    solver->set_initial_solution({{y, 5.0}});
    SolveResult result = solver->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.objective_value == Approx(13.0));

    solver->set_initial_solution(result.variable_values);
    SolveResult again = solver->solve();
    REQUIRE(again.optimal());
    REQUIRE(again.objective_value == Approx(13.0));
}

TEST_CASE("set_initial_solution rejects values it can't pass on", "[initial]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 4.0});
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, x);
    auto solver = create_solver(model, backend);

    Model other;
    SAIOPT_VAR(other, stranger);

    REQUIRE_THROWS_AS(solver->set_initial_solution({{x, 5.0}}), std::runtime_error);
    REQUIRE_THROWS_AS(solver->set_initial_solution({{x, std::nan("")}}), std::runtime_error);
    REQUIRE_THROWS_AS(solver->set_initial_solution({{stranger, 1.0}}), std::runtime_error);

    // Bounds are checked as they stand now, after any change made
    // through the Solver.
    solver->set_variable_upper(x, 10.0);
    REQUIRE_NOTHROW(solver->set_initial_solution({{x, 5.0}}));
    REQUIRE(solver->solve().objective_value == Approx(10.0));
}
