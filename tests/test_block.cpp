#include <stdexcept>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/model.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("nested blocks aggregate scaled objectives", "[block]") {
    Model model;

    Block widgets;
    Variable x = widgets.add_var(VariableDef::continuous().upper(10.0).name("x"));
    widgets.set_objective(5.0 * x);
    widgets.add_constraint("cap", x <= 10.0);

    Block gadgets;
    Variable y = gadgets.add_var(VariableDef::continuous().upper(4.0).name("y"));
    gadgets.set_objective(3.0 * y);
    gadgets.add_constraint("cap", y <= 4.0);

    model.add_block("widgets", std::move(widgets));
    model.add_block_scaled("gadgets", std::move(gadgets), 2.0);

    // Root itself contributes nothing; the tree's total objective is the
    // sum of each attached block's own objective, scaled by its multiplier.
    model.set_objective(ObjectiveSense::Maximize, LinearExpression{});

    REQUIRE(model.objective_coefficient(x) == Approx(5.0));
    REQUIRE(model.objective_coefficient(y) == Approx(6.0));  // 3.0 * multiplier 2.0

    SolveResult result = model.solve();
    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(74.0));  // 5*10 + 6*4
    REQUIRE(result.value(x) == Approx(10.0));
    REQUIRE(result.value(y) == Approx(4.0));

    const Block* found = model.child("widgets");
    REQUIRE(found != nullptr);
    REQUIRE(found->var("x").has_value());
    REQUIRE(model.child("nonexistent") == nullptr);
}

TEST_CASE("duplicate variable name in a block throws", "[block]") {
    Model model;
    model.add_var(VariableDef::continuous().name("dup"));
    REQUIRE_THROWS_AS(model.add_var(VariableDef::continuous().name("dup")), std::runtime_error);
}

TEST_CASE("duplicate constraint name in a block throws", "[block]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous());
    model.add_constraint("cap", x <= 5.0);
    REQUIRE_THROWS_AS(model.add_constraint("cap", x <= 5.0), std::runtime_error);
}

TEST_CASE("a block cannot reference an ancestor's variable", "[block]") {
    Model model;
    Variable r = model.add_var(VariableDef::continuous());

    Block child;
    child.add_constraint("bad", r <= 5.0);
    model.add_block("child", std::move(child));
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

    REQUIRE_THROWS_AS(model.solve(), std::runtime_error);
}
