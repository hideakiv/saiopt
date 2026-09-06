#pragma once

#include "saiopt/indexed.hpp"
#include "saiopt/var_spec.hpp"

// Convenience declaration macros: derive both a local C++ variable's name
// and its saiopt lookup name from a single identifier, so a later
// Model::var / Model::constraint / Model::block lookup doesn't need the
// name repeated as a separate string literal.
//
// Prefixed with SAIOPT_ (rather than bare VAR/CONSTRAINT/BLOCK) since C++
// preprocessor macros are global and unscoped, and those bare names would
// be too easy to collide with unrelated code.

// `kind` and `bounds` are both optional, keyword-style (C++20 designated
// initializers), and independent -- kind defaults to VarKind::Continuous,
// bounds default to unbounded:
//
//   SAIOPT_VAR(model, x);                                     // continuous, unbounded
//   SAIOPT_VAR(model, x, .bounds = {0.0, 6.0});                // continuous, bounded
//   SAIOPT_VAR(model, flag, .kind = VarKind::Binary);          // binary (bounds fixed at [0,1])
//   SAIOPT_VAR(model, n, .kind = VarKind::Integer, .bounds = {0.0, 10.0});
//
// See saiopt/var_spec.hpp for VarSpec.
#define SAIOPT_VAR(block, ident, ...) \
    [[maybe_unused]] auto ident =     \
        (block).add_var(#ident, ::saiopt::detail::make_var_def(::saiopt::detail::VarSpec{__VA_ARGS__}))

#define SAIOPT_CONSTRAINT(block, ident, expr) [[maybe_unused]] auto ident = (block).add_constraint(#ident, (expr))

#define SAIOPT_BLOCK(block, ident, child) [[maybe_unused]] auto ident = (block).add_block(#ident, (child))

#define SAIOPT_BLOCK_SCALED(block, ident, child, multiplier) \
    [[maybe_unused]] auto ident = (block).add_block_scaled(#ident, (child), (multiplier))

// The indexed counterpart to SAIOPT_VAR: declare one variable per item of
// an index, deriving each item's name from the same single identifier.
// `kind` and `bounds` work the same as SAIOPT_VAR (both optional, same
// defaults) and are uniform across the whole group by default -- but
// either may instead be a callable `key -> ...` for a per-item value
// looked up by key (not a parallel vector, which could drift out of
// sync with `index` positionally). A per-item `kind` is expected to be
// rare -- real models have so far only ever needed per-item `bounds` --
// but is supported the same way, for completeness:
//
//   SAIOPT_VARS(model, open, regions);                                    // continuous, unbounded
//   SAIOPT_VARS(model, open, regions, .kind = VarKind::Binary);           // binary, uniform
//   SAIOPT_VARS(model, x, routes, .bounds = {0.0, kInfinity});            // continuous, uniform bounds
//   SAIOPT_VARS(model, x, foods,
//               .bounds = [&](const std::string& k) { return std::tuple{0.0, cap.at(k)}; });
//   SAIOPT_VARS(model, x, items,
//               .kind = [&](const std::string& k) { return is_discrete.at(k) ? VarKind::Integer
//                                                                             : VarKind::Continuous; });
//
// See saiopt/var_spec.hpp for VarsSpec.
#define SAIOPT_VARS(block, ident, index, ...)                                                      \
    [[maybe_unused]] auto ident = (block).add_vars(#ident, (index), [&](const auto& saiopt_key) {  \
        return ::saiopt::detail::make_var_def(::saiopt::detail::VarsSpec{__VA_ARGS__}, saiopt_key); \
    })

// The indexed counterpart to SAIOPT_CONSTRAINT: declare one constraint per
// item of an index, deriving the group's name prefix from a single
// identifier.
#define SAIOPT_CONSTRAINTS(block, ident, index, f) \
    [[maybe_unused]] auto ident = (block).add_constraints(#ident, (index), (f))

// The indexed counterpart to SAIOPT_BLOCK / SAIOPT_BLOCK_SCALED: add one
// child block per item of an index, deriving each item's name from a
// single identifier.
//
// A closure passed to SAIOPT_CONSTRAINTS/add_constraints on the same model
// can't also look a sibling block up by name inside the same expression --
// build any cross-block constraint in a separate step after SAIOPT_BLOCKS
// completes, using either the handles the child-building closure returned
// some other way, or a fresh model.blocks(name, index) (or, one at a time,
// model.block(name)) lookup once every block is attached.
#define SAIOPT_BLOCKS(block, ident, index, child) \
    [[maybe_unused]] auto ident = (block).add_blocks(#ident, (index), (child))

#define SAIOPT_BLOCKS_SCALED(block, ident, index, child, multiplier) \
    [[maybe_unused]] auto ident = (block).add_blocks_scaled(#ident, (index), (child), (multiplier))

// set_objective has no name to derive -- an objective is never looked
// back up by string the way a var/constraint/block is -- so this is only
// a thin wrapper, for consistency with the macros above. Forwards
// whatever's passed after `target` to whichever set_objective overload
// matches: SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, expr) for a
// Model's root objective, or SAIOPT_OBJECTIVE(child, expr) for a Block's
// own local contribution.
#define SAIOPT_OBJECTIVE(target, ...) (target).set_objective(__VA_ARGS__)
