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
    Variable x = widgets.add_var("x", VariableDef::continuous().bound(0.0, 10.0));
    widgets.set_objective(5.0 * x);
    widgets.add_constraint("cap", x <= 10.0);

    Block gadgets;
    Variable y = gadgets.add_var("y", VariableDef::continuous().bound(0.0, 4.0));
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
    model.add_var("dup", VariableDef::continuous());
    REQUIRE_THROWS_AS(model.add_var("dup", VariableDef::continuous()), std::runtime_error);
}

TEST_CASE("duplicate constraint name in a block throws", "[block]") {
    Model model;
    Variable x = model.add_var("x", VariableDef::continuous());
    model.add_constraint("cap", x <= 5.0);
    REQUIRE_THROWS_AS(model.add_constraint("cap", x <= 5.0), std::runtime_error);
}

TEST_CASE("print labels nested-block variables by their qualified path", "[block]") {
    Model model;
    Variable x = model.add_var("x", VariableDef::continuous().bound(0.0, kInfinity));

    Block child;
    Variable y = child.add_var("y", VariableDef::continuous().bound(0.0, 4.0));
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
        Variable x = model.add_var("x", VariableDef::continuous());
        Block child;
        Variable y = child.add_var("y", VariableDef::continuous());
        child.set_objective(y);
        model.add_block_scaled("sub", std::move(child), 1.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + sub.(\n  y\n)");
    }

    SECTION("a negative multiplier subtracts instead of adding") {
        Model model;
        Variable x = model.add_var("x", VariableDef::continuous());
        Block child;
        Variable y = child.add_var("y", VariableDef::continuous());
        child.set_objective(y);
        model.add_block_scaled("sub", std::move(child), -2.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x - 2 sub.(\n  y\n)");
    }

    SECTION("an empty intermediate block contributes no stray parentheses") {
        Model model;
        Variable x = model.add_var("x", VariableDef::continuous());

        Block grandchild;
        Variable z = grandchild.add_var("z", VariableDef::continuous());
        grandchild.set_objective(z);

        Block child;  // no local objective, no own variables
        child.add_block_scaled("gc", std::move(grandchild), 5.0);

        model.add_block_scaled("sub", std::move(child), 2.0);
        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + 2 sub.(\n  5 gc.(\n    z\n  )\n)");
    }

    SECTION("no objective anywhere falls back to 0") {
        Model model;
        model.add_var("x", VariableDef::continuous());
        model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

        REQUIRE(objective_block(model) == "0");
    }

    SECTION("a sibling term shares a line with the previous one's close paren") {
        Model model;
        Variable x = model.add_var("x", VariableDef::continuous());

        Block p;
        Variable u = p.add_var("u", VariableDef::continuous());
        p.set_objective(u);
        model.add_block("p", std::move(p));  // default multiplier 1

        Block q;
        Variable w = q.add_var("w", VariableDef::continuous());
        q.set_objective(w);
        model.add_block_scaled("q", std::move(q), -3.0);

        model.set_objective(ObjectiveSense::Minimize, x);

        REQUIRE(objective_block(model) == "x + p.(\n  u\n) - 3 q.(\n  w\n)");
    }
}

TEST_CASE("a block cannot reference an ancestor's variable", "[block]") {
    Model model;
    Variable r = model.add_var("r", VariableDef::continuous());

    Block child;
    child.add_constraint("bad", r <= 5.0);
    model.add_block("child", std::move(child));
    model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

    REQUIRE_THROWS_AS(create_solver(model, "highs"), std::runtime_error);
}

TEST_CASE("Model(Block, sense) promotes a standalone Block, keeping its own objective", "[block]") {
    // A reusable builder that returns a self-contained Block, with no
    // idea yet whether it'll be solved on its own or attached as some
    // other Model's child.
    auto build_diet = []() {
        Block block;
        Variable bread = block.add_var("bread", VariableDef::continuous().bound(0.0, kInfinity));
        Variable milk = block.add_var("milk", VariableDef::continuous().bound(0.0, kInfinity));
        block.add_constraint("protein", 4.0 * bread + 8.0 * milk >= 20.0);
        block.set_objective(2.0 * bread + 3.0 * milk);
        return block;
    };

    SECTION("promoted directly and solved") {
        Model model(build_diet(), ObjectiveSense::Minimize);

        auto solver = create_solver(model, "highs");
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
        // All-milk (20/8=2.5 units, cost 7.5) beats all-bread (20/4=5
        // units, cost 10) -- milk is the cheaper protein source here.
        REQUIRE(result.objective_value == Approx(7.5));
    }

    SECTION("the same builder's Block attaches as a child just as well") {
        Model model;
        model.add_block_scaled("diet", build_diet(), 2.0);
        model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

        auto solver = create_solver(model, "highs");
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
        REQUIRE(result.objective_value == Approx(15.0));  // 7.5 * multiplier 2.0
    }

    SECTION("the single-argument constructor leaves sense unset until set_objective") {
        Model model{build_diet()};
        REQUIRE_THROWS_AS(create_solver(model, "highs"), std::runtime_error);

        // set_objective(sense, expr) replaces the promoted Block's own
        // objective, same as calling it twice on any Model always does.
        Variable bread = *model.var("bread");
        model.set_objective(ObjectiveSense::Minimize, bread);
        auto solver = create_solver(model, "highs");
        SolveResult result = solver->solve();
        REQUIRE(result.optimal);
        REQUIRE(result.objective_value == Approx(0.0));
    }
}

TEST_CASE("Model::to_block() hands a Model's content back as a plain Block", "[block]") {
    Model model;
    Variable x = model.add_var("x", VariableDef::continuous().bound(0.0, kInfinity));
    model.add_constraint("cap", x <= 4.0);
    model.set_objective(ObjectiveSense::Minimize, 2.0 * x);

    Block block = std::move(model).to_block();
    REQUIRE(block.var("x").has_value());
    REQUIRE(block.constraint("cap").has_value());

    // Attaching it as a child elsewhere folds its (former) objective in
    // as an ordinary scaled contribution -- sense_ is gone, as expected,
    // since only the new parent Model's own sense matters now.
    Model parent;
    parent.add_block_scaled("child", std::move(block), 5.0);
    parent.set_objective(ObjectiveSense::Maximize, LinearExpression{});

    auto solver = create_solver(parent, "highs");
    SolveResult result = solver->solve();
    REQUIRE(result.optimal);
    REQUIRE(result.objective_value == Approx(40.0));  // maximize 5 * (2x), x capped at 4 -> 10*4
}
