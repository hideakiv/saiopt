#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/cross.hpp"
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("SAIOPT_VAR declares a named local bound to the same lookup name", "[macros]") {
    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 4.0});

    REQUIRE(model.var("x") == x);
}

TEST_CASE("SAIOPT_CONSTRAINT declares a named local bound to the same lookup name", "[macros]") {
    Model model;
    SAIOPT_VAR(model, x, .bounds = {0.0, 4.0});
    SAIOPT_CONSTRAINT(model, cap, x <= 4.0);

    REQUIRE(model.constraint("cap") == cap);
}

TEST_CASE("SAIOPT_BLOCK / SAIOPT_BLOCK_SCALED attach a named child block", "[macros]") {
    Model model;
    Block child;
    SAIOPT_BLOCK_SCALED(model, sub, std::move(child), 0.5);

    REQUIRE(model.block("sub") != nullptr);
}

TEST_CASE("SAIOPT_VARS declares one variable per index item, named by key", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, open, items, .kind = VarKind::Binary);

    REQUIRE(open.size() == 2);
    REQUIRE(model.var("open[a]") == open[std::string("a")]);
    REQUIRE(model.var("open[b]") == open[std::string("b")]);
}

TEST_CASE("Model::vars/Block::vars look up a whole indexed group at once", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b", "c"};
    SAIOPT_VARS(model, open, items, .kind = VarKind::Binary);

    SECTION("matches the original handle, key for key") {
        auto looked_up = model.vars("open", items);

        REQUIRE(looked_up.size() == open.size());
        for (const auto& item : items) REQUIRE(looked_up[item] == open[item]);
    }

    SECTION("works the same way from inside a Block, not just Model's root") {
        Block block;
        std::vector<std::string> lines{"x", "y"};
        SAIOPT_VARS(block, output, lines, .bounds = {0.0, kInfinity});

        auto looked_up = block.vars("output", lines);
        REQUIRE(looked_up.size() == output.size());
        for (const auto& line : lines) REQUIRE(looked_up[line] == output[line]);
    }

    SECTION("throws naming the first key that wasn't declared") {
        std::vector<std::string> wrong_items{"a", "nope"};
        REQUIRE_THROWS_AS(model.vars("open", wrong_items), std::runtime_error);
    }
}

TEST_CASE("Model::constraints/Block::constraints look up a whole indexed group at once", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b", "c"};
    SAIOPT_VARS(model, x, items);
    SAIOPT_CONSTRAINTS(model, cap, items, ([&](const std::string& k) { return x[k] <= 4.0; }));

    SECTION("matches the original handle, key for key") {
        auto looked_up = model.constraints("cap", items);

        REQUIRE(looked_up.size() == cap.size());
        for (const auto& item : items) REQUIRE(looked_up[item] == cap[item]);
    }

    SECTION("works the same way from inside a Block, not just Model's root") {
        Block block;
        std::vector<std::string> lines{"p", "q"};
        SAIOPT_VARS(block, y, lines);
        SAIOPT_CONSTRAINTS(block, limit, lines, ([&](const std::string& k) { return y[k] <= 10.0; }));

        auto looked_up = block.constraints("limit", lines);
        REQUIRE(looked_up.size() == limit.size());
        for (const auto& line : lines) REQUIRE(looked_up[line] == limit[line]);
    }

    SECTION("throws naming the first key that wasn't declared") {
        std::vector<std::string> wrong_items{"a", "nope"};
        REQUIRE_THROWS_AS(model.constraints("cap", wrong_items), std::runtime_error);
    }
}

TEST_CASE("SAIOPT_VARS accepts a per-key lambda for bounds", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    std::map<std::string, double> caps{{"a", 4.0}, {"b", 9.0}};

    SAIOPT_VARS(model, x, items,
                .bounds = [&](const std::string& k) { return std::tuple{0.0, caps.at(k)}; });
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

    REQUIRE(x.size() == 2);
    auto solver = create_solver(model, "highs");
    REQUIRE(solver->variable_upper(x[std::string("a")]) == Approx(4.0));
    REQUIRE(solver->variable_upper(x[std::string("b")]) == Approx(9.0));
}

TEST_CASE("SAIOPT_VARS accepts a per-key lambda for kind", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    std::map<std::string, bool> is_discrete{{"a", true}, {"b", false}};

    SAIOPT_VARS(model, x, items, .kind = [&](const std::string& k) {
        return is_discrete.at(k) ? VarKind::Integer : VarKind::Continuous;
    });
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

    REQUIRE(x.size() == 2);
    auto solver = create_solver(model, "highs");
    REQUIRE(solver->variable_kind(x[std::string("a")]) == VarKind::Integer);
    REQUIRE(solver->variable_kind(x[std::string("b")]) == VarKind::Continuous);
}

TEST_CASE("SAIOPT_CONSTRAINTS declares one constraint per index item, named by prefix", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, x, items);
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
    REQUIRE(model.block("region[1]") != nullptr);
    REQUIRE(model.block("region[2]") != nullptr);
}

TEST_CASE("Model::blocks/Block::blocks look up a whole indexed group at once", "[macros]") {
    Model model;
    std::vector<int> regions{1, 2, 3};
    SAIOPT_BLOCKS(model, region, regions, ([](int) { return Block(); }));

    SECTION("matches the original handle, key for key") {
        auto looked_up = model.blocks("region", regions);

        REQUIRE(looked_up.size() == region.size());
        for (int r : regions) REQUIRE(looked_up[r] == &model.block(region[r]));
    }

    SECTION("works the same way from inside a Block, not just Model's root") {
        Block block;
        std::vector<int> lines{10, 20};
        SAIOPT_BLOCKS(block, sub, lines, ([](int) { return Block(); }));

        auto looked_up = block.blocks("sub", lines);
        REQUIRE(looked_up.size() == sub.size());
        for (int line : lines) REQUIRE(looked_up[line] == &block.block(sub[line]));
    }

    SECTION("throws naming the first key that wasn't declared") {
        std::vector<int> wrong_regions{1, 99};
        REQUIRE_THROWS_AS(model.blocks("region", wrong_regions), std::runtime_error);
    }
}

TEST_CASE("index-free vars/constraints/blocks retrieval: untyped and typed", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b", "c"};
    SAIOPT_VARS(model, open, items, .kind = VarKind::Binary);
    SAIOPT_CONSTRAINTS(model, cap, items, ([&](const std::string& k) { return open[k] <= 1.0; }));
    SAIOPT_BLOCKS(model, region, items, ([](const std::string&) { return Block(); }));

    SECTION("untyped vars(name)/constraints(name)/blocks(name) return every item, in declaration order") {
        std::vector<Variable> v = model.vars("open");
        std::vector<ConstraintRef> c = model.constraints("cap");
        std::vector<Block*> b = model.blocks("region");

        REQUIRE(v.size() == 3);
        REQUIRE(c.size() == 3);
        REQUIRE(b.size() == 3);
        for (std::size_t i = 0; i < items.size(); ++i) {
            REQUIRE(v[i] == open[items[i]]);
            REQUIRE(c[i] == cap[items[i]]);
            REQUIRE(b[i] == &model.block(region[items[i]]));
        }
    }

    SECTION("typed vars<K>(name)/constraints<K>(name)/blocks<K>(name) give back real key-based access") {
        Indexed<std::string, Variable> v = model.vars<std::string>("open");
        Indexed<std::string, ConstraintRef> c = model.constraints<std::string>("cap");
        Indexed<std::string, Block*> b = model.blocks<std::string>("region");

        for (const auto& item : items) {
            REQUIRE(v[item] == open[item]);
            REQUIRE(c[item] == cap[item]);
            REQUIRE(b[item] == &model.block(region[item]));
        }
    }

    SECTION("works the same way from inside a Block, not just Model's root") {
        Block block;
        std::vector<int> lines{10, 20};
        SAIOPT_VARS(block, y, lines);

        std::vector<Variable> untyped = block.vars("y");
        REQUIRE(untyped.size() == 2);

        Indexed<int, Variable> typed = block.vars<int>("y");
        for (int line : lines) REQUIRE(typed[line] == y[line]);
    }

    SECTION("throws for a name nothing was ever declared under") {
        REQUIRE_THROWS_AS(model.vars("nope"), std::runtime_error);
        REQUIRE_THROWS_AS(model.constraints("nope"), std::runtime_error);
        REQUIRE_THROWS_AS(model.blocks("nope"), std::runtime_error);
        REQUIRE_THROWS_AS(model.vars<std::string>("nope"), std::runtime_error);
    }

    SECTION("typed retrieval throws if K doesn't match the declared key type") {
        REQUIRE_THROWS_AS(model.vars<int>("open"), std::runtime_error);
    }

    SECTION("reusing a group name (of the same kind) throws") {
        REQUIRE_THROWS_AS(model.add_vars("open", items, [](const std::string&) { return VariableDef::continuous(); }),
                           std::runtime_error);
        REQUIRE_THROWS_AS(
            model.add_constraints("cap", items, [](const std::string&) { return LinearExpression{} <= 1.0; }),
            std::runtime_error);
        REQUIRE_THROWS_AS(model.add_blocks("region", items, [](const std::string&) { return Block(); }),
                           std::runtime_error);
    }
}

TEST_CASE("cross() builds flat multi-dimensional indices for indexed vars", "[macros]") {
    Model model;
    std::vector<std::string> plants{"p1", "p2"};
    std::vector<std::string> markets{"m1"};
    auto routes = cross(plants, markets);

    SAIOPT_VARS(model, ship, routes);

    REQUIRE(ship.size() == 2);
    REQUIRE(model.var("ship[(p1, m1)]").has_value());
    REQUIRE(model.var("ship[(p2, m1)]").has_value());
}

TEST_CASE("cross() stays flat past two ranges", "[macros]") {
    Model model;
    std::vector<std::string> plants{"p1", "p2"};
    std::vector<std::string> markets{"m1"};
    std::vector<int> periods{1, 2};
    auto route_periods = cross(plants, markets, periods);

    SAIOPT_VARS(model, ship, route_periods);

    // A flat std::tuple<std::string, std::string, int>, not
    // std::tuple<std::tuple<std::string, std::string>, int> -- so the
    // subscript and derived name are both flat, at any arity.
    REQUIRE(ship.size() == 4);
    Variable v = ship[{"p1", "m1", 2}];
    REQUIRE(model.var("ship[(p1, m1, 2)]") == v);
}

TEST_CASE("indexed vars solve and their values can be looked up in bulk", "[macros]") {
    Model model;
    std::vector<std::string> items{"a", "b"};
    SAIOPT_VARS(model, x, items, .bounds = {0.0, 5.0});
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
