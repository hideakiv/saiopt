#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "saiopt/cross.hpp"
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// A classic 2-index transportation problem: ship goods from warehouses to
// customers at minimum cost, respecting warehouse supply limits and
// customer demand. Demonstrates cross() for multi-dimensional
// (warehouse, customer) indices, and SAIOPT_VARS/SAIOPT_CONSTRAINTS -- the
// indexed counterparts of SAIOPT_VAR/SAIOPT_CONSTRAINT -- for declaring x
// and both constraint groups.
int main() {
    using namespace saiopt;

    std::vector<std::string> warehouses{"W1", "W2"};
    std::vector<std::string> customers{"C1", "C2", "C3"};

    std::map<std::string, double> supply{{"W1", 20.0}, {"W2", 30.0}};
    std::map<std::string, double> demand{{"C1", 15.0}, {"C2", 20.0}, {"C3", 15.0}};
    std::map<std::pair<std::string, std::string>, double> cost{
        {{"W1", "C1"}, 4.0}, {{"W1", "C2"}, 6.0}, {{"W1", "C3"}, 9.0},
        {{"W2", "C1"}, 5.0}, {{"W2", "C2"}, 3.0}, {{"W2", "C3"}, 2.0},
    };

    Model model;

    auto routes = cross(warehouses, customers);
    SAIOPT_VARS(model, x, routes,
                ([](const auto&) { return VariableDef::continuous().bound(0.0, kInfinity); }));

    // Never ship more than a warehouse has on hand.
    SAIOPT_CONSTRAINTS(model, supply_limit, warehouses, ([&](const std::string& w) {
                            LinearExpression shipped;
                            for (const auto& c : customers) shipped += x[{w, c}];
                            return shipped <= supply.at(w);
                        }));

    // Always meet every customer's demand.
    SAIOPT_CONSTRAINTS(model, demand_min, customers, ([&](const std::string& c) {
                            LinearExpression received;
                            for (const auto& w : warehouses) received += x[{w, c}];
                            return received >= demand.at(c);
                        }));

    LinearExpression total_cost;
    for (const auto& [w, c] : routes) {
        total_cost += cost.at({w, c}) * x[{w, c}];
    }
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, total_cost);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    std::printf("minimum shipping cost = %.2f\n", result.objective_value);
    for (const auto& [w, c] : routes) {
        double shipped = result.value(x[{w, c}]);
        if (shipped > 1e-6) {
            std::printf("  %s -> %s: %.2f units\n", w.c_str(), c.c_str(), shipped);
        }
    }
    return 0;
}
