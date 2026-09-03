#include <cassert>
#include <cstdio>
#include <utility>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"

// The smallest possible demonstration of SAIOPT_VAR, SAIOPT_CONSTRAINT, and
// SAIOPT_BLOCK_SCALED used together: every variable, constraint, and added
// block is declared and named in one step, with no .name("...") written
// separately from the local it matches.
//
// Root objective is just x; the child block contributes 2*y, scaled by a
// multiplier of 3 when added, so the total objective is x + 6*y. A root
// constraint (x + y >= 5) references both the root's own variable and the
// child's -- the allowed direction (a child can never reach back up to a
// parent's variables). Minimizing x + 6*y subject to x + y >= 5 should push
// y to 0 and x to 5.
int main() {
    using namespace saiopt;

    Model model;
    SAIOPT_VAR(model, x, VariableDef::continuous());

    Block child;
    SAIOPT_VAR(child, y, VariableDef::continuous());
    SAIOPT_CONSTRAINT(child, y_upper, y <= 4.0);
    child.set_objective(2.0 * y);
    SAIOPT_BLOCK_SCALED(model, sub, std::move(child), 3.0);

    SAIOPT_CONSTRAINT(model, min_total, x + y >= 5.0);
    model.set_objective(ObjectiveSense::Minimize, x);

    SolveResult result = model.solve();

    std::printf("minimum total cost = %.2f\n", result.objective_value);
    std::printf("  x = %.2f\n", result.value(x));
    std::printf("  y = %.2f\n", result.value(y));

    // None of the bindings above (x, y, sub) are needed to get back to the
    // same handles -- every name lives in the namespace of the block it was
    // declared on, so the whole tree can be walked back by name alone.
    Variable looked_up_x = *model.var("x");
    Variable looked_up_y = *model.child("sub")->var("y");
    assert(looked_up_x == x);
    assert(looked_up_y == y);
    std::printf("(name-based lookup confirmed to match the original handles)\n");
    return 0;
}
