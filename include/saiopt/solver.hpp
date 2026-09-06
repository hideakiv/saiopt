#pragma once

#include <functional>
#include <memory>
#include <string>

#include "saiopt/constraint_ref.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

class Model;
struct SolveResult;

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

    // Objective coefficient of `var` in the flattened LP.
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
};

// A backend's constructor, wrapped so it can be looked up by name. See
// SolverRegistration.
using SolverFactory = std::function<std::unique_ptr<Solver>(const Model&)>;

// Registers `factory` under `name` for later lookup by create_solver.
// Most code shouldn't call this directly -- see SolverRegistration below.
void register_solver(std::string name, SolverFactory factory);

// Registers a backend's factory as the side effect of constructing a
// static instance, so a backend can make itself available under a name
// just by being linked in -- e.g., in that backend's own .cpp file:
//
//   namespace {
//   SolverRegistration registration{"highs", [](const Model& model) {
//       return std::make_unique<HighsSolver>(model);
//   }};
//   }
struct SolverRegistration {
    SolverRegistration(std::string name, SolverFactory factory);
};

// Builds a Solver for `model` using the backend registered under
// `solver_name` (e.g. "highs", registered by saiopt_highs when it's
// linked in). Throws std::runtime_error if no backend is registered
// under that name.
std::unique_ptr<Solver> create_solver(const Model& model, const std::string& solver_name);

}  // namespace saiopt
