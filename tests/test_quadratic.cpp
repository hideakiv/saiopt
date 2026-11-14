#include <interfaces/highs_c_api.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

template <typename T>
concept Constrainable = requires(T expr) { expr <= 3.0; };

// SCIP's spatial branch-and-bound pins a quadratic objective's value down
// to its usual tolerances, but the objective is flat near its optimum, so
// the point it stops at is only accurate to about the square root of that.
Approx near(const std::string& backend, double value) {
    return backend == "scip" ? Approx(value).margin(1e-3) : Approx(value);
}

}  // namespace

TEST_CASE("quadratic expressions combine terms by unordered pair", "[quadratic]") {
    Model model;
    SAIOPT_VAR(model, x);
    SAIOPT_VAR(model, y);

    static_assert(std::is_same_v<decltype(x + y), LinearExpression>);
    static_assert(std::is_same_v<decltype(2.0 * x), LinearExpression>);
    static_assert(std::is_same_v<decltype(x * y), QuadraticExpression>);
    static_assert(std::is_same_v<decltype(x * y + x), QuadraticExpression>);
    static_assert(Constrainable<LinearExpression>);
    static_assert(!Constrainable<QuadraticExpression>);

    QuadraticExpression q = x * y + 2.0 * (y * x) + x * x;
    REQUIRE(q.coefficient(x, y) == 3.0);
    REQUIRE(q.coefficient(y, x) == 3.0);
    REQUIRE(q.coefficient(x, x) == 1.0);
    REQUIRE(q.coefficient(y, y) == 0.0);
    REQUIRE(q.quadratic_terms().size() == 2);

    // (x + 1)(y - 2) = xy - 2x + y - 2
    QuadraticExpression p = (x + 1.0) * (y - 2.0);
    REQUIRE(p.coefficient(x, y) == 1.0);
    REQUIRE(p.linear().coefficient(x) == -2.0);
    REQUIRE(p.linear().coefficient(y) == 1.0);
    REQUIRE(p.constant() == -2.0);

    QuadraticExpression scaled = -(2.0 * p - 3.0);
    REQUIRE(scaled.coefficient(x, y) == -2.0);
    REQUIRE(scaled.constant() == 7.0);

    q.set_coefficient(y, x, 0.0);
    REQUIRE(q.quadratic_terms().size() == 1);
}

TEST_CASE("a convex quadratic objective solves on both backends", "[quadratic]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    SECTION("minimize a sum of squares") {
        Model model;
        SAIOPT_VAR(model, x);
        SAIOPT_VAR(model, y);
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x * x + y * y);
        SAIOPT_CONSTRAINT(model, total, x + y >= 2.0);

        SolveResult result = create_solver(model, backend)->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.objective_value == Approx(2.0));
        REQUIRE(result.value(x) == near(backend, 1.0));
        REQUIRE(result.value(y) == near(backend, 1.0));
    }

    SECTION("maximize a concave function, with a constant") {
        Model model;
        SAIOPT_VAR(model, x, .bounds = {0.0, 5.0});
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, -1.0 * x * x + 4.0 * x + 1.0);

        SolveResult result = create_solver(model, backend)->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.objective_value == Approx(5.0));
        REQUIRE(result.value(x) == near(backend, 2.0));
    }

    SECTION("quadratic terms from sub-blocks are scaled by their multiplier") {
        Model model;
        SAIOPT_VAR(model, x, .bounds = {0.0, 10.0});

        Block sub;
        SAIOPT_VAR(sub, y, .bounds = {0.0, 10.0});
        sub.set_objective((y - 3.0) * (y - 3.0));
        model.add_block_scaled("sub", std::move(sub), 2.0);

        // 2 (y - 3)^2 + (x - y)^2 + x, minimized: x = y - 1/2 from the
        // x-gradient, then 4 (y - 3) + 1 = 0 from the y-gradient.
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, (x - y) * (x - y) + x);

        SolveResult result = create_solver(model, backend)->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.value(y) == near(backend, 2.75));
        REQUIRE(result.value(x) == near(backend, 2.25));
        REQUIRE(result.objective_value == Approx(2.0 * 0.0625 + 0.25 + 2.25));
    }
}

TEST_CASE("HiGHS publishes duals for a continuous QP but no ranging", "[quadratic]") {
    Model model;
    SAIOPT_VAR(model, x);
    SAIOPT_VAR(model, y);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x * x + y * y);
    SAIOPT_CONSTRAINT(model, total, x + y >= 2.0);

    auto solver = create_solver(model, "highs");
    solver->set_sensitivity_ranging(true);
    SolveResult result = solver->solve();
    REQUIRE(result.optimal());
    // d(objective)/d(rhs) at x = y = rhs / 2: d(rhs^2 / 2)/d(rhs) = rhs.
    REQUIRE(result.dual(total) == Approx(2.0));
    REQUIRE(result.objective_coefficient_ranges.empty());
}

TEST_CASE("only SCIP takes nonconvex or mixed-integer quadratic objectives", "[quadratic]") {
    SECTION("nonconvex: SCIP finds the global minimum") {
        Model model;
        SAIOPT_VAR(model, x, .bounds = {-1.0, 1.0});
        SAIOPT_VAR(model, y, .bounds = {-1.0, 1.0});
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x * y);

        SolveResult result = create_solver(model, "scip")->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.objective_value == Approx(-1.0));
    }

    SECTION("mixed-integer: SCIP solves it, HiGHS refuses it") {
        Model model;
        SAIOPT_VAR(model, x, .kind = VarKind::Integer, .bounds = {0.0, 5.0});
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, (x - 2.4) * (x - 2.4));

        SolveResult result = create_solver(model, "scip")->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.value(x) == Approx(2.0));
        REQUIRE(result.objective_value == Approx(0.16));

        REQUIRE_THROWS_AS(create_solver(model, "highs"), std::runtime_error);
    }

    SECTION("HiGHS also refuses making a variable integer afterward") {
        Model model;
        SAIOPT_VAR(model, x, .bounds = {0.0, 5.0});
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x * x);

        auto solver = create_solver(model, "highs");
        REQUIRE_THROWS_AS(solver->set_variable_kind(x, VarKind::Integer), std::runtime_error);
        REQUIRE(solver->variable_kind(x) == VarKind::Continuous);
    }
}

TEST_CASE("SCIP accepts a complete initial solution for a quadratic objective", "[quadratic]") {
    // Several independent integer squares, so the first solution SCIP
    // finds on its own isn't already the optimum.
    Model model;
    std::vector<int> index{0, 1, 2, 3, 4, 5, 6, 7};
    SAIOPT_VARS(model, x, index, .kind = VarKind::Integer, .bounds = {0.0, 10.0});
    QuadraticExpression objective;
    LinearExpression total;
    for (int i : index) {
        double target = 1.3 + 1.1 * i;
        objective += (x[i] - target) * (x[i] - target);
        total += x[i];
    }
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, objective);
    SAIOPT_CONSTRAINT(model, budget, total <= 30.0);

    SolveResult optimum = create_solver(model, "scip")->solve();
    REQUIRE(optimum.optimal());

    auto first_only = SolverOptions{}.set_int("limits/solutions", 1);
    SolveResult unseeded = create_solver(model, "scip", first_only)->solve();
    REQUIRE(unseeded.objective_value > optimum.objective_value + 1e-6);

    auto solver = create_solver(model, "scip", first_only);
    solver->set_initial_solution(optimum.variable_values);
    SolveResult seeded = solver->solve();
    REQUIRE(seeded.has_solution);
    REQUIRE(seeded.objective_value == Approx(optimum.objective_value));
}

TEST_CASE("a quadratic objective prints, serializes, and writes", "[quadratic]") {
    SECTION("print") {
        Model model;
        SAIOPT_VAR(model, x);
        SAIOPT_VAR(model, y);
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * x * y + y * y - x + 1.0);
        std::ostringstream out;
        model.print(out);
        REQUIRE(out.str().find("  2 x*y + y^2 - x + 1\n") != std::string::npos);
    }

    SECTION("serialize") {
        Block block;
        SAIOPT_VAR(block, x, .bounds = {0.0, 10.0});
        SAIOPT_VAR(block, y, .bounds = {0.0, 10.0});
        SAIOPT_CONSTRAINT(block, total, x + y >= 4.0);
        block.set_objective(x * x + 3.0 * y * y + x * y);

        Model model(Block::deserialize(block.serialize()), ObjectiveSense::Minimize);
        // Along x + y = 4 the gradients match when 2x + y = 6y + x, i.e.
        // x = 5y: y = 2/3, x = 10/3, objective (100 + 12 + 20) / 9.
        SolveResult result = create_solver(model, "highs")->solve();
        REQUIRE(result.optimal());
        REQUIRE(result.objective_value == Approx(132.0 / 9.0));
    }

    SECTION("write_model round-trips a HiGHS QP through LP and MPS files") {
        std::string extension = GENERATE(".lp", ".mps");
        CAPTURE(extension);

        Model model;
        SAIOPT_VAR(model, x);
        SAIOPT_VAR(model, y);
        SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, x * x + x * y + y * y - 3.0 * x);
        auto solver = create_solver(model, "highs");
        SolveResult result = solver->solve();
        REQUIRE(result.optimal());

        auto dir = std::filesystem::temp_directory_path() / "saiopt_test_quadratic";
        std::filesystem::create_directories(dir);
        auto path = dir / ("qp" + extension);
        solver->write_model(path.string());

        void* highs = Highs_create();
        Highs_setBoolOptionValue(highs, "output_flag", 0);
        HighsInt read_status = Highs_readModel(highs, path.c_str());
        Highs_run(highs);
        HighsInt model_status = Highs_getModelStatus(highs);
        double objective = Highs_getObjectiveValue(highs);
        Highs_destroy(highs);
        REQUIRE(read_status != kHighsStatusError);
        REQUIRE(model_status == kHighsModelStatusOptimal);
        REQUIRE(objective == Approx(result.objective_value));
    }
}
