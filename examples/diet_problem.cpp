#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// The classic "diet problem": choose how many units of each food to buy to
// minimize cost while meeting minimum calorie and protein requirements and
// staying under a fat limit, with a purchase cap on every food. Demonstrates
// indexed vars with per-index bounds via SAIOPT_VARS, and SAIOPT_CONSTRAINT
// for the three single, statically-named constraints.

namespace {

struct Food {
    std::string name;
    double cost;
    double max_units;
    double calories;
    double protein;
    double fat;
};

}  // namespace

int main() {
    using namespace saiopt;

    std::vector<Food> foods{
        {"Bread", 2.0, 4.0, 100.0, 4.0, 1.0},
        {"Milk", 3.5, 8.0, 150.0, 8.0, 5.0},
        {"Cheese", 8.0, 3.0, 110.0, 7.0, 9.0},
        {"Potato", 1.5, 10.0, 90.0, 2.0, 0.0},
    };

    const double min_calories = 2000.0;
    const double min_protein = 55.0;
    const double max_fat = 70.0;

    std::vector<std::string> names;
    std::map<std::string, const Food*> by_name;
    for (const auto& f : foods) {
        names.push_back(f.name);
        by_name[f.name] = &f;
    }

    Model model;

    SAIOPT_VARS(model, x, names, .bounds = [&](const std::string& name) {
        return std::tuple{0.0, by_name.at(name)->max_units};
    });

    LinearExpression calories_total;
    LinearExpression protein_total;
    LinearExpression fat_total;
    for (const auto& name : names) {
        const Food& f = *by_name.at(name);
        calories_total += f.calories * x[name];
        protein_total += f.protein * x[name];
        fat_total += f.fat * x[name];
    }

    SAIOPT_CONSTRAINT(model, calories_min, calories_total >= min_calories);
    SAIOPT_CONSTRAINT(model, protein_min, protein_total >= min_protein);
    SAIOPT_CONSTRAINT(model, fat_max, fat_total <= max_fat);

    LinearExpression total_cost;
    for (const auto& name : names) {
        total_cost += by_name.at(name)->cost * x[name];
    }
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, total_cost);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    std::printf("minimum cost = %.2f\n", result.objective_value);
    std::vector<std::string> sorted_names = names;
    std::sort(sorted_names.begin(), sorted_names.end());
    for (const auto& name : sorted_names) {
        std::printf("  %-8s %.3f units\n", name.c_str(), result.value(x[name]));
    }
    return 0;
}
