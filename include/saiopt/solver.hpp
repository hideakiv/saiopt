#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "saiopt/constraint_ref.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

class Model;
struct SolveResult;

// Backend-specific settings to apply when constructing a Solver, keyed
// by name (e.g. HiGHS's own option names, like "time_limit" or
// "mip_rel_gap") -- saiopt doesn't interpret these itself, a backend
// does, so any option a backend's own native API supports can be set
// here without saiopt needing to know about it in advance. Separate
// setters per value type (rather than one overloaded/variant-typed
// `set`) mirror HiGHS' own segregated option-setting API and sidestep
// the bool/integer overload-resolution ambiguity a single `set` would
// invite. Passing an option a backend doesn't recognize, or a value it
// rejects, throws std::runtime_error when the Solver is constructed --
// see the individual backend for exactly which options it supports.
class SolverOptions {
public:
    SolverOptions() = default;

    SolverOptions& set_bool(std::string key, bool value) {
        bool_options_[std::move(key)] = value;
        return *this;
    }
    SolverOptions& set_int(std::string key, std::int64_t value) {
        int_options_[std::move(key)] = value;
        return *this;
    }
    SolverOptions& set_double(std::string key, double value) {
        double_options_[std::move(key)] = value;
        return *this;
    }
    SolverOptions& set_string(std::string key, std::string value) {
        string_options_[std::move(key)] = std::move(value);
        return *this;
    }

    const std::map<std::string, bool>& bool_options() const noexcept { return bool_options_; }
    const std::map<std::string, std::int64_t>& int_options() const noexcept { return int_options_; }
    const std::map<std::string, double>& double_options() const noexcept { return double_options_; }
    const std::map<std::string, std::string>& string_options() const noexcept {
        return string_options_;
    }

private:
    std::map<std::string, bool> bool_options_;
    std::map<std::string, std::int64_t> int_options_;
    std::map<std::string, double> double_options_;
    std::map<std::string, std::string> string_options_;
};

// A snapshot of solver progress, reported periodically during solve()
// via Solver::set_progress_callback. `iterations` means whatever the
// current solve phase counts -- simplex or IPM iterations for an LP,
// total node-LP iterations for a MIP search. The MIP-only fields are
// unset (rather than a misleading 0) outside a MIP search.
struct SolverProgress {
    double elapsed_seconds = 0.0;
    std::int64_t iterations = 0;
    double objective_value = 0.0;

    std::optional<double> best_bound;        // MIP only
    std::optional<double> gap;               // MIP only
    std::optional<std::int64_t> node_count;  // MIP only
};

// Returning false requests the solve stop early (an interrupt) at the
// next opportunity the backend checks for one; the resulting SolveResult
// then reports non-optimal. Not every backend can honor an interrupt at
// every point it reports progress -- see the concrete backend.
//
// After an interrupted solve, calling solve() again on the *same*
// Solver is not guaranteed to behave like a fresh attempt -- see the
// concrete backend for what it actually does. Build a new Solver (via
// create_solver) for a clean re-attempt.
using ProgressCallback = std::function<bool(const SolverProgress&)>;

// A solver instance bound to one Model. Constructing a concrete
// implementation (e.g. HighsSolver) translates that Model into the
// backend's native form once; solve() can then be called repeatedly
// without re-flattening the Block tree each time.
//
// The coefficient/bound accessors below act directly on that backend's
// live, already-flattened state -- e.g. HighsSolver's implementations
// call straight into HiGHS' own change-coefficient API -- rather than on
// the Block tree, so a change here never requires re-flattening. Every
// Variable/ConstraintRef must be one that was present in the Model this
// Solver was built from; passing one that wasn't throws
// std::runtime_error.
//
// saiopt's core library depends only on this interface, never on a
// concrete backend.
class Solver {
public:
    virtual ~Solver() = default;
    virtual SolveResult solve() = 0;

    // Linear objective coefficient of `var` in the flattened model -- for
    // a quadratic objective, the coefficient of its linear part only;
    // its quadratic terms can't be changed through the Solver.
    virtual double objective_coefficient(Variable var) const = 0;
    virtual void set_objective_coefficient(Variable var, double coeff) = 0;

    // Coefficient of `var` in the constraint `cref`.
    virtual double constraint_coefficient(ConstraintRef cref, Variable var) const = 0;
    virtual void set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) = 0;

    virtual double constraint_lower(ConstraintRef cref) const = 0;
    virtual void set_constraint_lower(ConstraintRef cref, double lower) = 0;

    virtual double constraint_upper(ConstraintRef cref) const = 0;
    virtual void set_constraint_upper(ConstraintRef cref, double upper) = 0;

    // Bounds of `var` in the flattened LP.
    virtual double variable_lower(Variable var) const = 0;
    virtual void set_variable_lower(Variable var, double lower) = 0;

    virtual double variable_upper(Variable var) const = 0;
    virtual void set_variable_upper(Variable var, double upper) = 0;

    // Continuous/integer/binary kind of `var`.
    virtual VarKind variable_kind(Variable var) const = 0;
    virtual void set_variable_kind(Variable var, VarKind kind) = 0;

    // Registers `callback` to run periodically during solve() (and every
    // subsequent solve() on this Solver, until changed); pass {} to
    // clear it. Backends differ in exactly how often this fires -- see
    // the concrete backend.
    virtual void set_progress_callback(ProgressCallback callback) = 0;

    // Writes this Solver's current model -- including any coefficient/
    // bound/kind changes made through the setters above -- to `path`,
    // in the file format its extension names. Every backend supports
    // ".lp" (CPLEX LP) and ".mps" (free MPS); see the concrete backend
    // for any others. Variables and constraints are written under their
    // qualified names (see SparseLp::col_name), rewritten by
    // file_safe_names into identifiers the format accepts. Throws
    // std::runtime_error if the backend can't write `path` or doesn't
    // recognize its extension.
    virtual void write_model(const std::string& path) const = 0;

    // Gives the next solve() a starting point -- a MIP start: a value for
    // some or all variables (e.g. a previous SolveResult's
    // variable_values), which the backend tries to complete into a
    // feasible solution and, if it succeeds, starts its search from as
    // an incumbent. It's only a hint: a start that can't be completed
    // into a feasible solution is dropped, and the solve proceeds as if
    // none had been given. Neither backend can withdraw a start once
    // given, so calling this again adds a new one rather than replacing
    // the old -- whether an earlier start (or a solution an earlier
    // solve() found) also still seeds a later solve() is up to the
    // backend. An empty `values` does nothing. Throws std::runtime_error
    // -- before handing anything to the backend -- if a variable doesn't
    // belong to this Solver's model, or a value isn't finite or lies
    // outside that variable's current bounds.
    virtual void set_initial_solution(const std::unordered_map<Variable, double>& values) = 0;
};

// A backend's constructor, wrapped so it can be looked up by name. See
// SolverRegistration.
using SolverFactory = std::function<std::unique_ptr<Solver>(const Model&, const SolverOptions&)>;

// Registers `factory` under `name` for later lookup by create_solver.
// Most code shouldn't call this directly -- see SolverRegistration below.
void register_solver(std::string name, SolverFactory factory);

// Registers a backend's factory as the side effect of constructing a
// static instance, so a backend can make itself available under a name
// just by being linked in -- e.g., in that backend's own .cpp file:
//
//   namespace {
//   SolverRegistration registration{"highs", [](const Model& model, const SolverOptions& options) {
//       return std::make_unique<HighsSolver>(model, options);
//   }};
//   }
struct SolverRegistration {
    SolverRegistration(std::string name, SolverFactory factory);
};

// Builds a Solver for `model` using the backend registered under
// `solver_name` (e.g. "highs", registered by saiopt_highs when it's
// linked in), applying `options` (see SolverOptions), e.g.:
//
//   auto solver = create_solver(model, "highs",
//       SolverOptions{}.set_double("time_limit", 30.0).set_bool("output_flag", true));
//
// Throws std::runtime_error if no backend is registered under that name,
// or if the backend rejects one of `options`.
std::unique_ptr<Solver> create_solver(const Model& model, const std::string& solver_name,
                                       const SolverOptions& options = SolverOptions{});

}  // namespace saiopt
