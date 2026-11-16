"""Structured Algebraic Interface for Optimization.

Build a model out of composable Blocks -- each with its own variables,
linear constraints, and (linear or quadratic) objective contribution --
then solve it with a registered backend ("highs" or "scip")::

    import saiopt as so

    model = so.Model()
    model.bread = so.Var(bounds=(0, so.inf))
    model.milk = so.Var(bounds=(0, so.inf))
    model.protein = so.Constraint(4 * model.bread + 8 * model.milk >= 20)
    model.set_objective(so.ObjectiveSense.Minimize, 2 * model.bread + 3 * model.milk)

    result = so.create_solver(model, "highs").solve()
    print(result.status, result.objective_value, result[model.bread])

Assigning to an attribute declares something under that name (see
so.Var, so.Vars, so.Constraint, so.Constraints, so.Blocks, so.Scaled,
so.PiecewiseLinear); the add_* methods do the same for names that
aren't Python identifiers.
"""

from ._saiopt import (
    Block,
    Constraint,
    ConstraintRef,
    LinearExpression,
    Model,
    ObjectiveSense,
    Piecewise,
    PiecewiseMethod,
    QuadraticExpression,
    SensitivityRange,
    SolveResult,
    SolveStatus,
    Solver,
    SolverProgress,
    Variable,
    VarKind,
    create_solver,
    inf,
    quicksum,
)

from ._attributes import Blocks, Constraints, PiecewiseLinear, Scaled, Var, Vars

__all__ = [
    "Blocks",
    "Constraints",
    "PiecewiseLinear",
    "Scaled",
    "Var",
    "Vars",
    "Block",
    "Constraint",
    "ConstraintRef",
    "LinearExpression",
    "Model",
    "ObjectiveSense",
    "Piecewise",
    "PiecewiseMethod",
    "QuadraticExpression",
    "SensitivityRange",
    "SolveResult",
    "SolveStatus",
    "Solver",
    "SolverProgress",
    "Variable",
    "VarKind",
    "create_solver",
    "inf",
    "quicksum",
]
