#include <cstdio>
#include <map>
#include <string>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/set.hpp"
#include "saiopt/solver.hpp"

// A small model shaped to use every indexed macro together: SAIOPT_VARS for
// an indexed root decision, SAIOPT_BLOCKS for an indexed batch of attached
// child blocks, and SAIOPT_CONSTRAINTS for indexed root constraints that
// reference each block's own variable.
//
// Unlike examples/hierarchical_facilities.cpp, nothing here needs a child
// block's variable *while* that block is being built -- so SAIOPT_BLOCKS
// can attach every region's block in one call, and only afterward does a
// separate pass look each one's y variable back up by name, ready to hand
// to SAIOPT_CONSTRAINTS.
//
// Each region can open (open[r]) up to a production capacity of 10
// (y[r] <= 10 * open[r]); total production across all regions must reach
// at least 5. Minimizing the number of open regions should open exactly
// one (any single region's capacity already covers the requirement).
int main() {
    using namespace saiopt;

    Set<std::string> regions{"north", "south", "east"};

    Model model;

    SAIOPT_VARS(model, open, regions, ([](const std::string&) { return VariableDef::binary(); }));

    SAIOPT_BLOCKS(model, region, regions, ([](const std::string&) {
                      Block child;
                      SAIOPT_VAR(child, y, VariableDef::continuous().bound(0.0, 10.0));
                      return child;
                  }));

    // SAIOPT_BLOCKS only hands back one BlockRef per region -- to
    // reference each region's own y in a root constraint, look it back up
    // by name now that every block is attached (can't do this from inside
    // a SAIOPT_CONSTRAINTS/add_constraints closure -- see SAIOPT_BLOCKS'
    // docs).
    std::map<std::string, Variable> y;
    for (const auto& name : regions) {
        Block* block = model.child("region[" + name + "]");
        y.emplace(name, *block->var("y"));
    }

    SAIOPT_CONSTRAINTS(model, capacity, regions,
                        ([&](const std::string& name) { return y.at(name) <= 10.0 * open[name]; }));

    LinearExpression total_y;
    for (const auto& name : regions) total_y += y.at(name);
    SAIOPT_CONSTRAINT(model, min_total, total_y >= 5.0);

    LinearExpression total_open;
    for (const auto& name : regions) total_open += open[name];
    model.set_objective(ObjectiveSense::Minimize, total_open);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    std::printf("regions opened = %.0f\n", result.objective_value);
    for (const auto& name : regions) {
        bool is_open = result.value(open[name]) > 0.5;
        std::printf("  %-6s open=%-5s y=%.2f\n", name.c_str(), is_open ? "true" : "false",
                    result.value(y.at(name)));
    }
    return 0;
}
