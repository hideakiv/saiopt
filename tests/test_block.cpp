#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;
using Catch::Approx;

TEST_CASE("nested blocks aggregate scaled objectives", "[block]") {
    Model model;

    Block widgets;
    Variable x = widgets.add_var(VariableDef::continuous().bound(0.0, 10.0).name("x"));
    widgets.set_objective(5.0 * x);
    widgets.add_constraint("cap", x <= 10.0);

    Block gadgets;
    Variable y = gadgets.add_var(VariableDef::continuous().bound(0.0, 4.0).name("y"));
    gadgets.set_objective(3.0 * y);
    gadgets.add_constraint("cap", y <= 4.0);

    model.add_block("widgets", std::move(widgets));
    model.add_block_scaled("gadgets", std::move(gadgets), 2.0);

    // Root itself contributes nothing; the tree's total objective is the
    // sum of each attached block's own objective, scaled by its multiplier.
    model.set_objective(ObjectiveSense::Maximize, LinearExpression{});

    auto solver = create_solver(model, "highs");
    REQUIRE(solver->objective_coefficient(x) == Approx(5.0));
    REQUIRE(solver->objective_coefficient(y) == Approx(6.0));  // 3.0 * multiplier 2.0

    SolveResult result = solver->solve();
    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(74.0));  // 5*10 + 6*4
    REQUIRE(result.value(x) == Approx(10.0));
    REQUIRE(result.value(y) == Approx(4.0));

    const Block* found = model.block("widgets");
    REQUIRE(found != nullptr);
    REQUIRE(found->var("x").has_value());
    REQUIRE(model.block("nonexistent") == nullptr);
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

TEST_CASE("print labels nested-block variables by their qualified path", "[block]") {
    Model model;
    Variable x = model.add_var(VariableDef::continuous().bound(0.0, kInfinity).name("x"));

    Block child;
    Variable y = child.add_var(VariableDef::continuous().bound(0.0, 4.0).name("y"));
    child.add_constraint("y_upper", y <= 4.0);
    child.set_objective(2.0 * y);
    model.add_block_scaled("sub", std::move(child), 3.0);

    model.add_constraint("min_total", x + y >= 5.0);
    model.set_objective(ObjectiveSense::Minimize, x);

    std::ostringstream out;
    model.print(out);
    std::string text = out.str();

    REQUIRE(text.find("model (minimize)") != std::string::npos);
    REQUIRE(text.find("objective:\n  x + 3 sub.(\n    2 y\n  )\n") != std::string::npos);
    REQUIRE(text.find("min_total: sub.y + x >= 5") != std::string::npos);
    REQUIRE(text.find("block sub:") != std::string::npos);
    REQUIRE(text.find("y_upper: sub.y <= 4") != std::string::npos);
}

TEST_CASE("print folds nested objectives with the right sign, multiplier, and indentation", "[block]") {
    // Returns the objective section's own lines (the "objective:" header
    // itself dropped, and each line's two-space base indent stripped back
    // off) joined by "\n", so a nested case's expected shape can be
    // written the same way format_total_objective_lines itself builds it,
    // just with its own indent-2 base indent removed.
    auto objective_block = [](const Model& model) {
        std::ostringstream out;
        model.print(out);
        std::istringstream in(out.str());
        std::string line;
        std::vector<std::string> lines;
        bool collecting = false;
        while (std::getline(in, line)) {
            if (line == "objective:") {
                collecting = true;
                continue;
            }
            if (!collecting) continue;
            if (line.rfind("  ", 0) != 0) break;  // first non-indented line ends the section
            lines.push_back(line.substr(2));
        }
        std::string joined;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i) joined += "\n";
            joined += lines[i];
        }
        return joined;
    };

    SECTION("a multiplier of exactly 1 gets no numeric prefix") {
        Model model;
        Variable x = model.add_var(VariableDef::continuous().name("x"));
        Block child;
        Variable y = child.add_var(VariableDef::continuous().name("y"));
        child.set_objective(y);
        model.add_block_scaled("sub", std::move(child), 1.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + sub.(\n  y\n)");
    }

    SECTION("a negative multiplier subtracts instead of adding") {
        Model model;
        Variable x = model.add_var(VariableDef::continuous().name("x"));
        Block child;
        Variable y = child.add_var(VariableDef::continuous().name("y"));
        child.set_objective(y);
        model.add_block_scaled("sub", std::move(child), -2.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x - 2 sub.(\n  y\n)");
    }

    SECTION("an empty intermediate block contributes no stray parentheses") {
        Model model;
        Variable x = model.add_var(VariableDef::continuous().name("x"));

        Block grandchild;
        Variable z = grandchild.add_var(VariableDef::continuous().name("z"));
        grandchild.set_objective(z);

        Block child;  // no local objective, no own variables
        child.add_block_scaled("gc", std::move(grandchild), 5.0);

        model.add_block_scaled("sub", std::move(child), 2.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + 2 sub.(\n  5 gc.(\n    z\n  )\n)");
    }

    SECTION("no objective anywhere falls back to 0") {
        Model model;
        model.add_var(VariableDef::continuous().name("x"));
        model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

        REQUIRE(objective_block(model) == "0");
    }

    SECTION("a sibling term shares a line with the previous one's close paren") {
        Model model;
        Variable x = model.add_var(VariableDef::continuous().name("x"));

        Block p;
        Variable u = p.add_var(VariableDef::continuous().name("u"));
        p.set_objective(u);
        model.add_block("p", std::move(p));  // default multiplier 1

        Block q;
        Variable w = q.add_var(VariableDef::continuous().name("w"));
        q.set_objective(w);
        model.add_block_scaled("q", std::move(q), -3.0);

        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + p.(\n  u\n) - 3 q.(\n  w\n)");
    }
}

TEST_CASE("a block cannot reference an ancestor's variable", "[block]") {
    Model model;
    Variable r = model.add_var(VariableDef::continuous());

    Block child;
    child.add_constraint("bad", r <= 5.0);
    model.add_block("child", std::move(child));
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

    REQUIRE_THROWS_AS(create_solver(model, "highs"), std::runtime_error);
}
