#pragma once

#include "saiopt/indexed.hpp"

// Convenience declaration macros: derive both a local C++ variable's name
// and its saiopt lookup name from a single identifier, so a later
// Model::var / Model::constraint / Model::child lookup doesn't need the
// name repeated as a separate string literal.
//
//   SAIOPT_VAR(model, x, VariableDef::continuous().upper(4.0));
//   // expands to: auto x = model.add_var(VariableDef::continuous().upper(4.0).name("x"));
//
// Prefixed with SAIOPT_ (rather than bare VAR/CONSTRAINT/BLOCK) since C++
// preprocessor macros are global and unscoped, and those bare names would
// be too easy to collide with unrelated code.

#define SAIOPT_VAR(block, ident, def) [[maybe_unused]] auto ident = (block).add_var((def).name(#ident))

#define SAIOPT_CONSTRAINT(block, ident, expr) [[maybe_unused]] auto ident = (block).add_constraint(#ident, (expr))

#define SAIOPT_BLOCK(block, ident, child) [[maybe_unused]] auto ident = (block).add_block(#ident, (child))

#define SAIOPT_BLOCK_SCALED(block, ident, child, multiplier) \
    [[maybe_unused]] auto ident = (block).add_block_scaled(#ident, (child), (multiplier))

// The indexed counterpart to SAIOPT_VAR: declare one variable per item of
// an index, deriving each item's name from the same single identifier.
// `def` must not already call .name(...) itself -- the macro's own naming
// always overwrites it, same as SAIOPT_VAR.
#define SAIOPT_VARS(block, ident, index, def)                                                   \
    [[maybe_unused]] auto ident = (block).add_vars((index), [&](const auto& saiopt_key) {                        \
        return (def)(saiopt_key).name(::saiopt::detail::indexed_name(#ident, saiopt_key));      \
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
// some other way, or a fresh model.child(...) lookup once the block is
// attached.
#define SAIOPT_BLOCKS(block, ident, index, child) \
    [[maybe_unused]] auto ident = (block).add_blocks(#ident, (index), (child))

#define SAIOPT_BLOCKS_SCALED(block, ident, index, child, multiplier) \
    [[maybe_unused]] auto ident = (block).add_blocks_scaled(#ident, (index), (child), (multiplier))
