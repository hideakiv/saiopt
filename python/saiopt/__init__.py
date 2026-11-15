"""Structured Algebraic Interface for Optimization.

Build a model out of composable Blocks -- each with its own variables,
linear constraints, and (linear or quadratic) objective contribution --
then solve it with a registered backend ("highs" or "scip")::

    import saiopt as so

    model = so.Model()
    bread = model.add_var("bread", bounds=(0, so.inf))
    milk = model.add_var("milk", bounds=(0, so.inf))
    model.add_constraint("protein", 4 * bread + 8 * milk >= 20)
    model.set_objective(so.ObjectiveSense.Minimize, 2 * bread + 3 * milk)

    result = so.create_solver(model, "highs").solve()
    print(result.status, result.objective_value, result[bread])
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

__all__ = [
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
