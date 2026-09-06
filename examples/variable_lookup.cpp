#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// Demonstrates looking a Variable back up by name once its own handle is
// out of scope -- here, because it was declared inside a function that
// builds a child block and attaches it, handing back nothing but the
// Model itself. blocks_basics.cpp touches on the same lookup in passing
// (re-deriving a handle that's still in scope, just to show it matches);
// this example is built so the lookup is the *only* way to reach these
// variables at all -- build_region()'s own locals are long gone by the
// time main() runs.
//
// Model::var/Block::var only search that block's own variables (not its
// descendants); Model::block/Block::block (by name -- an overload of the
// same method that also takes a BlockRef) descends one level, so a
// variable nested `depth` blocks deep takes `depth - 1` block() calls
// before the final var(). Neither throws on a miss -- var/constraint
// come back an empty std::optional, block comes back nullptr -- so a
// lookup chain can be probed safely instead of crashing on a typo'd
// path, as the "missing" lookups below show.
//
// An indexed group (SAIOPT_VARS/add_vars, and the same for
// SAIOPT_CONSTRAINTS/SAIOPT_BLOCKS) is remembered as a group under its
// own name, retrievable two ways once the original handle is out of
// scope (see "output" below):
//   - vars<K>(name): the real Indexed<K, Variable> the declaration built,
//     with key-based access back -- `K` has to be given explicitly
//     (it can't be deduced from `name` alone), e.g. vars<std::string>(...).
//   - vars(name): the same items as a plain std::vector<Variable>, in
//     declaration order, when even `K` isn't known or doesn't matter.
// Both throw std::runtime_error if no such group was ever declared;
// vars<K> also throws if the group exists but was declared with a
// different key type than `K`.

namespace {

// Builds a two-level-deep block -- "capacity" and an indexed "output"
// group directly in it, "loss" in a further nested child -- and attaches
// it under `model` as "region". None of these Variables are returned;
// once this function is done, they're reachable only by name.
void build_region(saiopt::Model& model) {
    using namespace saiopt;

    Block region_block;
    SAIOPT_VAR(region_block, capacity, .bounds = {0.0, kInfinity});

    std::vector<std::string> lines{"a", "b", "c"};
    SAIOPT_VARS(region_block, output, lines, .bounds = {0.0, kInfinity});

    Block metering;
    SAIOPT_VAR(metering, loss, .bounds = {0.0, 5.0});
    SAIOPT_BLOCK(region_block, metering_block, std::move(metering));

    // A distinct local name (region_block) from the attach-name (region)
    // -- SAIOPT_BLOCK's `ident` both names the local BlockRef it declares
    // and, stringified, the name it's attached under, so reusing "region"
    // for both here would have the new declaration shadow itself in its
    // own initializer.
    SAIOPT_BLOCK(model, region, std::move(region_block));
}

}  // namespace

int main() {
    using namespace saiopt;

    Model model;
    build_region(model);

    const Block* region = model.block("region");
    if (!region) {
        std::fprintf(stderr, "region not found\n");
        return 1;
    }

    // "capacity" -- a plain named variable, one level deep.
    std::optional<Variable> capacity = region->var("capacity");

    // "output[a]"/"output[b]"/"output[c]" -- SAIOPT_VARS declared them as
    // a group named "output" in this same block. vars<std::string>("output")
    // gets the whole group back as the same Indexed<std::string, Variable>
    // shape SAIOPT_VARS itself returned inside build_region() -- with
    // key-based access -- using nothing but the group's name.
    Indexed<std::string, Variable> output = region->vars<std::string>("output");

    // The untyped form, for when even the key type isn't known -- just
    // every Variable in the group, in declaration order.
    std::vector<Variable> output_list = region->vars("output");

    // "loss" -- two levels deep, via the nested "metering_block" child.
    const Block* metering = region->block("metering_block");
    std::optional<Variable> loss = metering ? metering->var("loss") : std::nullopt;

    // A miss comes back empty/null for var()/block(); vars<K>()/vars()
    // throw std::runtime_error instead -- for an unknown group name, or
    // (typed only) the wrong key type -- rather than silently doing the
    // wrong thing.
    bool missing_var_found = region->var("nope").has_value();
    bool missing_block_found = model.block("no_such_region") != nullptr;
    bool vars_threw_on_unknown_group = false;
    try {
        region->vars("no_such_group");
    } catch (const std::runtime_error&) {
        vars_threw_on_unknown_group = true;
    }
    bool vars_threw_on_wrong_key_type = false;
    try {
        region->vars<int>("output");  // "output" was declared with std::string keys, not int
    } catch (const std::runtime_error&) {
        vars_threw_on_wrong_key_type = true;
    }

    if (!capacity || !loss || output_list.size() != output.size() || missing_var_found || missing_block_found ||
        !vars_threw_on_unknown_group || !vars_threw_on_wrong_key_type) {
        std::fprintf(stderr, "lookup didn't behave as expected\n");
        return 1;
    }
    std::printf("all lookups resolved correctly (including the expected misses)\n");

    // From here on, *capacity, output[key], and *loss are ordinary
    // Variables -- build_region()'s own locals are gone, but nothing
    // downstream can tell the difference. `lines` here is just main()'s
    // own iteration order for building the rest of the model -- unlike
    // the vars<K> lookup above, it's not needed to reach "output" itself.
    std::vector<std::string> lines{"a", "b", "c"};
    std::map<std::string, double> demand{{"a", 3.0}, {"b", 4.0}, {"c", 2.0}};
    SAIOPT_CONSTRAINTS(model, demand_met, lines,
                        ([&](const std::string& line) { return output[line] >= demand.at(line); }));

    LinearExpression total_output;
    for (const auto& [key, v] : output) total_output += v;
    SAIOPT_CONSTRAINT(model, capacity_limit, *capacity >= total_output + *loss);
    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, *capacity);

    auto solver = create_solver(model, "highs");
    SolveResult result = solver->solve();

    std::printf("minimum capacity = %.2f\n", result.value(*capacity));
    for (const auto& line : lines) {
        std::printf("  output[%s] = %.2f\n", line.c_str(), result.value(output[line]));
    }
    std::printf("  loss = %.2f\n", result.value(*loss));
    return 0;
}
