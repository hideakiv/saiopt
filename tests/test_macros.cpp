#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/set.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("SAIOPT_VAR declares a named local bound to the same lookup name", "[macros]") {
    Model model;
    SAIOPT_VAR(model, x, VariableDef::continuous().bound(0.0, 4.0));

    REQUIRE(model.var("x") == x);
}

TEST_CASE("SAIOPT_CONSTRAINT declares a named local bound to the same lookup name", "[macros]") {
    Model model;
    SAIOPT_VAR(model, x, VariableDef::continuous().bound(0.0, 4.0));
    SAIOPT_CONSTRAINT(model, cap, x <= 4.0);

    REQUIRE(model.constraint("cap") == cap);
}

TEST_CASE("SAIOPT_BLOCK / SAIOPT_BLOCK_SCALED attach a named child block", "[macros]") {
    Model model;
    Block child;
    SAIOPT_BLOCK_SCALED(model, sub, std::move(child), 0.5);

    REQUIRE(model.child("sub") != nullptr);
}

TEST_CASE("SAIOPT_VARS declares one variable per index item, named by key", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, open, items, ([](const std::string&) { return VariableDef::binary(); }));

    REQUIRE(open.size() == 2);
    REQUIRE(model.var("open[a]") == open[std::string("a")]);
    REQUIRE(model.var("open[b]") == open[std::string("b")]);
}

TEST_CASE("SAIOPT_CONSTRAINTS declares one constraint per index item, named by prefix", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, x, items, ([](const std::string&) { return VariableDef::continuous(); }));
    SAIOPT_CONSTRAINTS(model, cap, items,
                        ([&](const std::string& k) { return x[k] <= 4.0; }));

    REQUIRE(cap.size() == 2);
    REQUIRE(model.constraint("cap[a]") == cap[std::string("a")]);
}

TEST_CASE("SAIOPT_BLOCKS_SCALED attaches one child per index item, named by prefix", "[macros]") {
    Model model;
    std::vector<int> regions{1, 2};
    SAIOPT_BLOCKS_SCALED(model, region, regions, ([](int) { return Block(); }),
                          ([](int) { return 0.5; }));

    REQUIRE(region.size() == 2);
    REQUIRE(model.child("region[1]") != nullptr);
    REQUIRE(model.child("region[2]") != nullptr);
}

TEST_CASE("Set::cross builds two-dimensional indices for indexed vars", "[macros]") {
    Model model;
    Set<std::string> plants{"p1", "p2"};
    Set<std::string> markets{"m1"};
    Set<std::pair<std::string, std::string>> routes = plants.cross(markets);

    SAIOPT_VARS(model, ship, routes, ([](const auto&) { return VariableDef::continuous(); }));

    REQUIRE(ship.size() == 2);
    REQUIRE(model.var("ship[(p1, m1)]").has_value());
    REQUIRE(model.var("ship[(p2, m1)]").has_value());
}

TEST_CASE("indexed vars solve and their values can be looked up in bulk", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, x, items, ([](const std::string&) { return VariableDef::continuous().bound(0.0, 5.0); }));
    SAIOPT_CONSTRAINTS(model, cap, items, ([&](const std::string& k) { return x[k] <= 5.0; }));

    LinearExpression total;
    for (const auto& [k, v] : x) {
        (void)k;
        total += v;
    }
    model.set_objective(ObjectiveSense::Maximize, total);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();
    REQUIRE(result.optimal);

    Indexed<std::string, double> values = result.values(x);
    REQUIRE(values[std::string("a")] == Approx(5.0));
    REQUIRE(values[std::string("b")] == Approx(5.0));
}
