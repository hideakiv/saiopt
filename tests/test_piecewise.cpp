#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

// Up, down, up again: nonconvex, so a plain convex combination of the
// breakpoints (dropping SOS2 or integrality) would undercut it.
const std::vector<double> kPoints{0.0, 2.0, 4.0, 6.0};
const std::vector<double> kValues{0.0, 4.0, 1.0, 6.0};

struct Case {
    PiecewiseMethod method;
    const char* backend;
};

}  // namespace

TEST_CASE("minimizes a nonconvex piecewise function", "[piecewise]") {
    auto [method, backend] = GENERATE(Case{PiecewiseMethod::Sos2, "scip"},
                                      Case{PiecewiseMethod::Incremental, "scip"},
                                      Case{PiecewiseMethod::Incremental, "highs"});
    CAPTURE(backend, method == PiecewiseMethod::Sos2);

    Model model;
    SAIOPT_VAR(model, x, .bounds = {1.0, kInfinity});
    Piecewise f = model.add_piecewise("f", x, kPoints, kValues, method);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, f.value);

    SolveResult result = create_solver(model, backend)->solve();
    REQUIRE(result.optimal());
    // The convex combination of (0, 0) and (4, 1) with x = 1 would be 0.25.
    REQUIRE(result.objective_value == Approx(1.0));
    REQUIRE(result.value(x) == Approx(4.0));
}

TEST_CASE("constrains a variable to a piecewise function, with x fixed between points", "[piecewise]") {
    auto method = GENERATE(PiecewiseMethod::Sos2, PiecewiseMethod::Incremental);

    Model model;
    SAIOPT_VAR(model, x, .bounds = {3.0, 3.0});
    SAIOPT_VAR(model, y);
    Piecewise f = model.add_piecewise("f", x, kPoints, kValues, method);
    SAIOPT_CONSTRAINT(model, y_is_f, y == f.value);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, y);

    SolveResult result = create_solver(model, "scip")->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.value(y) == Approx(2.5));  // halfway from (2, 4) to (4, 1)
}

TEST_CASE("a repeated point makes a jump", "[piecewise]") {
    auto method = GENERATE(PiecewiseMethod::Sos2, PiecewiseMethod::Incremental);
    // 1 below x = 1, 3 above it; at x = 1 itself, anything in [1, 3].
    std::vector<double> points{0.0, 1.0, 1.0, 2.0};
    std::vector<double> values{1.0, 1.0, 3.0, 3.0};

    auto solve_at = [&](double at, ObjectiveSense sense) {
        Model model;
        SAIOPT_VAR(model, x, .bounds = {at, at});
        Piecewise f = model.add_piecewise("f", x, points, values, method);
        SAIOPT_OBJECTIVE(model, sense, f.value);
        SolveResult result = create_solver(model, "scip")->solve();
        REQUIRE(result.optimal());
        return result.objective_value;
    };

    REQUIRE(solve_at(0.5, ObjectiveSense::Maximize) == Approx(1.0));
    REQUIRE(solve_at(1.5, ObjectiveSense::Minimize) == Approx(3.0));
    REQUIRE(solve_at(1.0, ObjectiveSense::Minimize) == Approx(1.0));
    REQUIRE(solve_at(1.0, ObjectiveSense::Maximize) == Approx(3.0));
}

TEST_CASE("x can be an expression, and values a callable", "[piecewise]") {
    Model model;
    SAIOPT_VAR(model, a, .bounds = {1.0, 1.0});
    SAIOPT_VAR(model, b, .bounds = {0.5, 0.5});
    std::vector<double> points{0.0, 1.0, 2.0, 3.0};
    SAIOPT_PIECEWISE(model, square, a + b, points, ([](double p) { return p * p; }));
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, square.value);

    // Between (1, 1) and (2, 4), at 1.5.
    REQUIRE(create_solver(model, "scip")->solve().objective_value == Approx(2.5));
}

TEST_CASE("x is limited to the range of the points", "[piecewise]") {
    auto method = GENERATE(PiecewiseMethod::Sos2, PiecewiseMethod::Incremental);
    Model model;
    SAIOPT_VAR(model, x);
    SAIOPT_PIECEWISE(model, f, x, kPoints, kValues, method);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, x);

    SolveResult result = create_solver(model, "scip")->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.value(x) == Approx(6.0));
}

TEST_CASE("the auxiliary variables live in a child block named after the function", "[piecewise]") {
    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 6.0});
    SAIOPT_PIECEWISE(model, sos, x, kPoints, kValues);
    SAIOPT_PIECEWISE(model, inc, x, kPoints, kValues, PiecewiseMethod::Incremental);

    REQUIRE(model.block("sos") == &model.block(sos.block));
    REQUIRE(model.block("sos")->vars("lambda").size() == 4);
    REQUIRE(model.block("sos")->constraint("convexity").has_value());
    REQUIRE(model.block("inc")->vars("delta").size() == 3);
    REQUIRE(model.block("inc")->vars("full").size() == 2);
    REQUIRE(model.constraint("sos").has_value());
    REQUIRE(model.constraint("inc").has_value());
}

TEST_CASE("bad breakpoints throw and leave the block unchanged", "[piecewise]") {
    double nan = std::numeric_limits<double>::quiet_NaN();
    auto [points, values, message] = GENERATE_COPY(
        std::tuple{std::vector<double>{0.0, 1.0}, std::vector<double>{0.0}, "2 points but 1 values"},
        std::tuple{std::vector<double>{0.0}, std::vector<double>{0.0}, "at least two points"},
        std::tuple{std::vector<double>{0.0, 2.0, 1.0}, std::vector<double>{0.0, 1.0, 2.0}, "non-decreasing"},
        std::tuple{std::vector<double>{0.0, kInfinity}, std::vector<double>{0.0, 1.0}, "isn't finite"},
        std::tuple{std::vector<double>{0.0, 1.0}, std::vector<double>{nan, 1.0}, "isn't finite"});

    Model model;
    SAIOPT_VAR(model, x);
    REQUIRE_THROWS_WITH(model.add_piecewise("f", x, points, values),
                        ContainsSubstring("\"f\"") && ContainsSubstring(message));
    REQUIRE(model.block("f") == nullptr);
    REQUIRE_FALSE(model.constraint("f").has_value());
}

TEST_CASE("a name already used by a child block or constraint throws", "[piecewise]") {
    Model model;
    SAIOPT_VAR(model, x);
    SAIOPT_CONSTRAINT(model, taken, x >= 0.0);
    SAIOPT_BLOCK(model, child, Block{});
    REQUIRE_THROWS_WITH(model.add_piecewise("taken", x, kPoints, kValues), ContainsSubstring("constraint"));
    REQUIRE(model.block("taken") == nullptr);
    REQUIRE_THROWS_WITH(model.add_piecewise("child", x, kPoints, kValues), ContainsSubstring("child block"));
    REQUIRE_FALSE(model.constraint("child").has_value());
}

TEST_CASE("HiGHS rejects an SOS2 set, naming the way out", "[piecewise][sos2]") {
    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 6.0});
    SAIOPT_PIECEWISE(model, f, x, kPoints, kValues);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, f.value);
    REQUIRE_THROWS_WITH(create_solver(model, "highs"),
                        ContainsSubstring("SOS2") && ContainsSubstring("f.sos2") &&
                            ContainsSubstring("Incremental"));
}

TEST_CASE("add_sos2 lets at most two neighbouring variables be nonzero", "[sos2]") {
    Model model;
    SAIOPT_VARS(model, v, std::vector<int>({0, 1, 2}), .bounds = {0.0, 1.0});
    model.add_sos2("pick", {v[0], v[1], v[2]});
    // Without the set, all three would be 1.
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, 2.0 * v[0] + 3.0 * v[1] + v[2]);

    SolveResult result = create_solver(model, "scip")->solve();
    REQUIRE(result.objective_value == Approx(5.0));  // v0 and v1, the best neighbouring pair
    REQUIRE(result.value(v[2]) == Approx(0.0));
}

TEST_CASE("add_sos2 checks its arguments", "[sos2]") {
    Model model;
    SAIOPT_VAR(model, a);
    SAIOPT_VAR(model, b);
    REQUIRE_THROWS_WITH(model.add_sos2("one", {a}), ContainsSubstring("at least two"));
    model.add_sos2("s", {a, b});
    REQUIRE_THROWS_WITH(model.add_sos2("s", {a, b}), ContainsSubstring("already exists"));
}

TEST_CASE("an SOS2 set may only use variables from its own block's subtree", "[sos2]") {
    Model model;
    SAIOPT_VAR(model, outside);
    Block child;
    SAIOPT_VAR(child, inside);
    child.add_sos2("s", {inside, outside});
    model.add_block("child", std::move(child));
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, LinearExpression{});
    REQUIRE_THROWS_WITH(create_solver(model, "scip"), ContainsSubstring("SOS2 sets"));
}
