#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("a flat block round-trips through serialize/deserialize", "[serialize]") {
    Block block;
    SAIOPT_VAR(block, x, .bounds = {0.0, 6.0});
    SAIOPT_VAR(block, y, .bounds = {0.0, kInfinity});
    SAIOPT_CONSTRAINT(block, total, x + y >= 5.0);
    block.set_objective(2.0 * x + 3.0 * y);

    Block rebuilt = Block::deserialize(block.serialize());

    Model model;
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});
    model.add_block("sub", std::move(rebuilt));

    const Block* sub = model.block("sub");
    REQUIRE(sub != nullptr);
    REQUIRE(sub->var("x").has_value());
    REQUIRE(sub->var("y").has_value());
    REQUIRE(sub->constraint("total").has_value());

    // A wrong constraint bound or objective coefficient surviving the
    // round-trip would change this optimum: satisfying x + y >= 5 entirely
    // with x (cost 2) beats routing any of it through y (cost 3).
    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.objective_value == Approx(10.0));
    REQUIRE(result.value(*sub->var("x")) == Approx(5.0));
    REQUIRE(result.value(*sub->var("y")) == Approx(0.0));
}

TEST_CASE("nested blocks with cross-block references round-trip and still solve", "[serialize]") {
    Block child;
    Variable y = child.add_var("y", VariableDef::continuous().bound(0.0, 4.0));
    child.add_constraint("y_upper", y <= 4.0);
    child.set_objective(2.0 * y);

    Block root;
    Variable x = root.add_var("x", VariableDef::continuous().bound(0.0, kInfinity));
    root.add_block_scaled("sub", std::move(child), 3.0);
    // A parent constraint referencing the (already-built) child's variable.
    root.add_constraint("min_total", x + y >= 5.0);
    root.set_objective(x);

    Block rebuilt = Block::deserialize(root.serialize());

    Model model;
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});
    model.add_block("root", std::move(rebuilt));

    const Block* rebuilt_root = model.block("root");
    REQUIRE(rebuilt_root != nullptr);
    const Block* rebuilt_sub = rebuilt_root->block("sub");
    REQUIRE(rebuilt_sub != nullptr);

    Variable new_x = *rebuilt_root->var("x");
    Variable new_y = *rebuilt_sub->var("y");
    ConstraintRef new_min_total = *rebuilt_root->constraint("min_total");

    // The reconstructed constraint must reference the *new* handles, not
    // the original ones from before serialization.
    auto solver = create_solver(model, "highs");
    REQUIRE(solver->constraint_coefficient(new_min_total, new_x) == Approx(1.0));
    REQUIRE(solver->constraint_coefficient(new_min_total, new_y) == Approx(1.0));
    REQUIRE(solver->objective_coefficient(new_y) == Approx(6.0));  // 2.0 * multiplier 3.0

    SolveResult result = solver->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.objective_value == Approx(5.0));
}

TEST_CASE("a variable's name round-trips exactly", "[serialize]") {
    Block block;
    Variable v = block.add_var("v", VariableDef::continuous().bound(0.0, 1.0));
    block.set_objective(v);

    Block rebuilt = Block::deserialize(block.serialize());
    REQUIRE(rebuilt.var("v").has_value());
}

TEST_CASE("deserialize rejects a truncated buffer", "[serialize]") {
    Block block;
    SAIOPT_VAR(block, x);
    block.set_objective(x);

    std::vector<std::byte> bytes = block.serialize();
    bytes.resize(bytes.size() / 2);

    REQUIRE_THROWS_AS(Block::deserialize(bytes), std::runtime_error);
}

TEST_CASE("serialize rejects a constraint referencing an out-of-subtree variable", "[serialize]") {
    Model model;
    Variable r = model.add_var("r", VariableDef::continuous());

    Block child;
    child.add_constraint("bad", r <= 5.0);

    REQUIRE_THROWS_AS(child.serialize(), std::runtime_error);
}

TEST_CASE("an SOS2 set round-trips through serialize/deserialize", "[serialize][sos2]") {
    // The same nonconvex function as test_piecewise.cpp: without its SOS2
    // set, the minimum would drop from 1 to 0.25.
    std::vector<double> points{0.0, 2.0, 4.0, 6.0};
    std::vector<double> values{0.0, 4.0, 1.0, 6.0};
    Block region;
    SAIOPT_VAR(region, x, .bounds = {1.0, kInfinity});
    SAIOPT_PIECEWISE(region, f, x, points, values);
    region.set_objective(f.value);

    Model model(Block::deserialize(region.serialize()), ObjectiveSense::Minimize);
    REQUIRE(model.block("f") != nullptr);

    SolveResult result = create_solver(model, "scip")->solve();
    REQUIRE(result.optimal());
    REQUIRE(result.objective_value == Approx(1.0));
    REQUIRE_THROWS_WITH(create_solver(model, "highs"), Catch::Matchers::ContainsSubstring("SOS2"));
}
