#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// A transportation problem over a *sparse* (jagged) network: each
// warehouse can only reach some of the customers, and the reachable set
// is a different length per warehouse -- not the full cartesian product
// cross() would give (see saiopt/cross.hpp). Demonstrates building the
// arc index by hand instead: flatten the jagged reachability map into
// the arcs that actually exist, then derive both the per-warehouse and
// per-customer constraint groups from that one flat list, rather than
// typing the reachability data out twice in each direction.
int main() {
    using namespace saiopt;

    std::map<std::string, std::vector<std::string>> reachable{
        {"W1", {"C1", "C2"}},
        {"W2", {"C2", "C3", "C4"}},
        {"W3", {"C1", "C4"}},
    };
    std::map<std::string, double> supply{{"W1", 20.0}, {"W2", 25.0}, {"W3", 15.0}};
    std::map<std::string, double> demand{{"C1", 15.0}, {"C2", 20.0}, {"C3", 10.0}, {"C4", 15.0}};
    std::map<std::pair<std::string, std::string>, double> cost{
        {{"W1", "C1"}, 4.0}, {{"W1", "C2"}, 6.0},
        {{"W2", "C2"}, 3.0}, {{"W2", "C3"}, 5.0}, {{"W2", "C4"}, 4.0},
        {{"W3", "C1"}, 7.0}, {{"W3", "C4"}, 2.0},
    };

    // Flatten the jagged reachability map into the arcs that actually
    // exist -- this is the index, built once and reused below.
    std::vector<std::tuple<std::string, std::string>> arcs;
    for (const auto& [w, customers] : reachable) {
        for (const auto& c : customers) arcs.emplace_back(w, c);
    }

    Model model;
    SAIOPT_VARS(model, ship, arcs, .bounds = {0.0, kInfinity});

    // Per-warehouse capacity: sum only over its own reachable customers.
    // Indexed by the map's *keys*, not the map itself -- iterating a map
    // directly would make each item's key a pair<const Key, Value>,
    // dragging the whole std::vector value into the derived name.
    std::vector<std::string> warehouses;
    for (const auto& [w, customers] : reachable) warehouses.push_back(w);
    SAIOPT_CONSTRAINTS(model, supply_limit, warehouses, ([&](const std::string& w) {
                            LinearExpression shipped;
                            for (const auto& c : reachable.at(w)) shipped += ship[{w, c}];
                            return shipped <= supply.at(w);
                        }));

    // Per-customer demand: derive who can reach each customer from the
    // same arcs list (the reverse of `reachable`), rather than typing a
    // second reachability map by hand that could drift out of sync.
    std::map<std::string, std::vector<std::string>> reaching;
    for (const auto& [w, c] : arcs) reaching[c].push_back(w);

    std::vector<std::string> customers;
    for (const auto& [c, warehouses_reaching] : reaching) {
        (void)warehouses_reaching;
        customers.push_back(c);
    }
    SAIOPT_CONSTRAINTS(model, demand_min, customers, ([&](const std::string& c) {
                            LinearExpression received;
                            for (const auto& w : reaching.at(c)) received += ship[{w, c}];
                            return received >= demand.at(c);
                        }));

    LinearExpression total_cost;
    for (const auto& [w, c] : arcs) total_cost += cost.at({w, c}) * ship[{w, c}];
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, total_cost);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    if (!result.optimal) {
        std::printf("infeasible\n");
        return 0;
    }
    std::printf("minimum shipping cost = %.2f  (%zu arcs used, out of a possible %zux%zu grid)\n",
                result.objective_value, arcs.size(), warehouses.size(), customers.size());
    for (const auto& [w, c] : arcs) {
        double shipped = result.value(ship[{w, c}]);
        if (shipped > 1e-6) std::printf("  %s -> %s: %.2f units\n", w.c_str(), c.c_str(), shipped);
    }
    return 0;
}
