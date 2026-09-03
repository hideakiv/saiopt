#include <cstdio>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/set.hpp"

// A small facility-location model built out of Blocks: the root Model
// decides which facilities to open (one indexed open[f] variable per
// facility, via SAIOPT_VARS over a Set of facility names), while each
// facility's own period-by-period production plan is a standalone Block.
//
// Every facility's block has the same shape: indexed produce variables (via
// SAIOPT_VARS), a single produce_total variable linked to their sum by an
// internal constraint, and a local objective over produce_total. Because
// nothing about a specific facility needs to flow back out of the
// block-building step beyond the Block itself (any of it -- produce,
// produce_total -- can always be found again by name), every facility's
// block can be added in one batch with SAIOPT_BLOCKS_SCALED, and the root's
// constraints can be built with SAIOPT_CONSTRAINTS afterward, referencing
// each facility's produce_total via a name-based lookup pass done once
// every block is attached (a closure passed to
// SAIOPT_CONSTRAINTS/add_constraints can't also borrow model to look a
// sibling block up itself -- see examples/indexed_macros.cpp for the same
// pattern in its simplest possible form).

namespace {

struct Facility {
    std::string name;
    double fixed_cost;
    double total_capacity;
    double period_capacity;
    double production_cost;
    // Scales this facility's production cost when it's rolled into the
    // total objective (e.g. a per-region overhead surcharge).
    double multiplier;
};

}  // namespace

int main() {
    using namespace saiopt;

    const int periods = 3;
    const double demand = 90.0;
    std::vector<Facility> facilities{
        {"North", 50.0, 100.0, 40.0, 2.0, 1.0},
        {"South", 30.0, 80.0, 30.0, 1.5, 1.2},
    };

    std::vector<std::string> names;
    std::map<std::string, const Facility*> by_name;
    for (const auto& f : facilities) {
        names.push_back(f.name);
        by_name[f.name] = &f;
    }
    Set<std::string> facility_names(names);

    std::vector<int> period_range(periods);
    std::iota(period_range.begin(), period_range.end(), 0);

    Model model;
    SAIOPT_VARS(model, open, facility_names,
                ([](const std::string&) { return VariableDef::binary(); }));

    SAIOPT_BLOCKS_SCALED(
        model, facility, facility_names,
        ([&](const std::string& name) {
            const Facility& f = *by_name.at(name);
            Block sub;

            SAIOPT_VARS(sub, produce, period_range, ([&](int) {
                            return VariableDef::continuous().upper(f.period_capacity);
                        }));
            SAIOPT_VAR(sub, produce_total, VariableDef::continuous());

            LinearExpression produce_sum;
            for (const auto& [period, var] : produce) {
                (void)period;
                produce_sum += var;
            }
            SAIOPT_CONSTRAINT(sub, produce_total_link, produce_total == produce_sum);

            sub.set_objective(f.production_cost * produce_total);
            return sub;
        }),
        ([&](const std::string& name) { return by_name.at(name)->multiplier; }));

    // SAIOPT_BLOCKS_SCALED only hands back one BlockRef per facility -- to
    // reference each facility's own produce_total in a root constraint,
    // look it back up by name now that every block is attached.
    std::map<std::string, Variable> produce_total_of;
    for (const auto& name : facility_names) {
        Block* fb = model.child("facility[" + name + "]");
        produce_total_of.emplace(name, *fb->var("produce_total"));
    }

    SAIOPT_CONSTRAINTS(model, capacity, facility_names, ([&](const std::string& name) {
                            return produce_total_of.at(name) <=
                                   by_name.at(name)->total_capacity * open[name];
                        }));

    LinearExpression total_production;
    for (const auto& name : facility_names) total_production += produce_total_of.at(name);
    SAIOPT_CONSTRAINT(model, meet_demand, total_production >= demand);

    LinearExpression fixed_cost_total;
    for (const auto& f : facilities) fixed_cost_total += f.fixed_cost * open[f.name];
    model.set_objective(ObjectiveSense::Minimize, fixed_cost_total);

    SolveResult result = model.solve();

    std::printf("minimum total cost = %.2f\n", result.objective_value);
    for (const auto& f : facilities) {
        bool is_open = result.value(open[f.name]) > 0.5;
        double produced = result.value(produce_total_of.at(f.name));
        std::printf("  %-6s open=%-5s produced=%.2f\n", f.name.c_str(), is_open ? "true" : "false",
                    produced);
    }
    return 0;
}
