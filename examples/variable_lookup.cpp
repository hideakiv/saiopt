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
// An indexed group of variables (SAIOPT_VARS/add_vars) can be looked back
// up the same way, one key at a time -- but Block::vars/Model::vars (the
// collection counterpart to var()) does that whole loop for you, handing
// back an Indexed<K, Variable> exactly like the original SAIOPT_VARS call
// did (see "output" below). Unlike var(), it throws if any expected key
// is missing, since a whole indexed group either was declared with that
// exact index or wasn't. indexed_macros.cpp relies on the one-at-a-time
// form for a similar reason -- there, each key's variable lives alone in
// its own differently-named child block, so there's no single block for
// vars() to search.

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

    // "output[a]"/"output[b]"/"output[c]" -- SAIOPT_VARS named each one
    // "output[<key>]" in this same block; vars() replays that naming to
    // get the whole group back in one call, the same Indexed<K, Variable>
    // shape SAIOPT_VARS itself returned inside build_region().
    std::vector<std::string> lines{"a", "b", "c"};
    Indexed<std::string, Variable> output = region->vars("output", lines);

    // "loss" -- two levels deep, via the nested "metering_block" child.
    const Block* metering = region->block("metering_block");
    std::optional<Variable> loss = metering ? metering->var("loss") : std::nullopt;

    // A miss comes back empty/null for var()/block(), or -- for vars() --
    // as a thrown std::runtime_error naming the missing key, rather than
    // silently doing the wrong thing.
    bool missing_var_found = region->var("nope").has_value();
    bool missing_block_found = model.block("no_such_region") != nullptr;
    bool vars_threw_on_missing_key = false;
    try {
        region->vars("output", std::vector<std::string>{"a", "nope"});
    } catch (const std::runtime_error&) {
        vars_threw_on_missing_key = true;
    }

    if (!capacity || !loss || missing_var_found || missing_block_found || !vars_threw_on_missing_key) {
        std::fprintf(stderr, "lookup didn't behave as expected\n");
        return 1;
    }
    std::printf("all lookups resolved correctly (including the expected misses)\n");

    // From here on, *capacity, output[key], and *loss are ordinary
    // Variables -- build_region()'s own locals are gone, but nothing
    // downstream can tell the difference.
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
