#include <cstdio>

#include "saiopt/model.hpp"

// Classic diet problem: choose quantities of foods to meet nutrient minimums
// at least cost.
int main() {
    using namespace saiopt;

    Model model;

    Variable bread = model.add_var(VariableDef::continuous().name("bread"));
    Variable milk = model.add_var(VariableDef::continuous().name("milk"));

    // Cost in dollars per unit.
    model.set_objective(ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);

    // Nutrient minimums per unit of food.
    model.add_constraint("protein", 4.0 * bread + 8.0 * milk >= 20.0);
    model.add_constraint("calories", 100.0 * bread + 150.0 * milk >= 400.0);

    SolveResult result = model.solve();

    if (!result.optimal) {
        std::printf("No optimal solution found.\n");
        return 1;
    }

    std::printf("Optimal cost: %.4f\n", result.objective_value);
    std::printf("  bread = %.4f\n", result.value(bread));
    std::printf("  milk  = %.4f\n", result.value(milk));
    return 0;
}
