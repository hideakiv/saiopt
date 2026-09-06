#pragma once

#include <tuple>
#include <type_traits>

#include "saiopt/infinity.hpp"
#include "saiopt/variable.hpp"

// Support types for SAIOPT_VAR / SAIOPT_VARS (see macros.hpp): let a
// variable's kind and bounds be given as keyword-style designated
// initializers, both optional, rather than spelling out a VariableDef --
// e.g. SAIOPT_VAR(model, x, .bounds = {0.0, 6.0}) or
// SAIOPT_VAR(model, flag, .kind = VarKind::Binary).

namespace saiopt::detail {

VariableDef build_var_def(VarKind kind, double lower, double upper);

// SAIOPT_VAR's spec: kind defaults to Continuous, bounds default to
// unbounded. Bounds are always a plain (lower, upper) tuple here -- a
// single variable has no per-index dimension for a lambda to vary over.
struct VarSpec {
    VarKind kind = VarKind::Continuous;
    std::tuple<double, double> bounds{-kInfinity, kInfinity};
};

inline VariableDef make_var_def(const VarSpec& spec) {
    const auto& [lower, upper] = spec.bounds;
    return build_var_def(spec.kind, lower, upper);
}

// SAIOPT_VARS's spec: same defaults as VarSpec, but `kind` and/or
// `bounds` may each instead be a callable `key -> ...` for a per-index
// value -- e.g. .bounds = [&](const auto& k) { return std::tuple{0.0,
// capacity.at(k)}; } -- looked up by key, so it can't drift out of sync
// with `index` the way a parallel vector positionally could. `kind`
// varying per key is expected to be rare (every real use so far only
// needed it for `bounds`), but works the same way for consistency.
// Templated so CTAD deduces which shape was given for each field
// independently; when a field is omitted entirely, its deduction falls
// back to its own default template argument, same as VarSpec's default.
template <typename Kind = VarKind, typename Bounds = std::tuple<double, double>>
struct VarsSpec {
    Kind kind = VarKind::Continuous;
    Bounds bounds{-kInfinity, kInfinity};
};

template <typename Kind, typename Bounds, typename Key>
VariableDef make_var_def(const VarsSpec<Kind, Bounds>& spec, const Key& key) {
    VarKind kind;
    if constexpr (std::is_invocable_v<const Kind&, const Key&>) {
        kind = spec.kind(key);
    } else {
        kind = spec.kind;
    }
    if constexpr (std::is_invocable_v<const Bounds&, const Key&>) {
        const auto& [lower, upper] = spec.bounds(key);
        return build_var_def(kind, lower, upper);
    } else {
        const auto& [lower, upper] = spec.bounds;
        return build_var_def(kind, lower, upper);
    }
}

}  // namespace saiopt::detail
