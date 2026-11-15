"""Declaring a block's contents by attribute assignment.

``model.x = so.Var(bounds=(0, 10))`` adds a variable named ``x`` to
``model``, and reading ``model.x`` later looks it up again. The value
assigned says what to declare: one of the types below, a ``Constraint``
(any comparison such as ``x + y >= 2``), or a standalone ``Block``.

The attribute name becomes the saiopt name, so it must be a Python
identifier. Names that aren't, or that are computed at runtime, still
need the ``add_*`` methods.

Model and Block's ``__setattr__`` calls :func:`set_attribute`. Reading
is handled in C++ (``__getattr__`` in python/src/blocks.cpp).
"""

from __future__ import annotations

from typing import Any, Callable, Iterable

from . import _saiopt


class Var:
    """One variable: ``model.x = so.Var(kind=..., bounds=(lower, upper))``.

    ``kind`` defaults to Continuous and ``bounds`` to unbounded, as in
    ``add_var``.
    """

    __slots__ = ("kind", "bounds")

    def __init__(self, *, kind: Any = None, bounds: Any = None) -> None:
        self.kind = kind
        self.bounds = bounds

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_var(name, **_given(kind=self.kind, bounds=self.bounds))

    def __repr__(self) -> str:
        return f"Var({_args(kind=self.kind, bounds=self.bounds)})"


class Vars:
    """One variable per key: ``model.ship = so.Vars(routes, bounds=(0, so.inf))``.

    Each is named ``name[key]``, and reading ``model.ship`` gives a dict
    from key to Variable. ``kind`` and ``bounds`` may each be a callable
    taking the key, as in ``add_vars``.
    """

    __slots__ = ("keys", "kind", "bounds")

    def __init__(self, keys: Iterable[Any], *, kind: Any = None, bounds: Any = None) -> None:
        self.keys = keys
        self.kind = kind
        self.bounds = bounds

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_vars(name, self.keys, **_given(kind=self.kind, bounds=self.bounds))

    def __repr__(self) -> str:
        return f"Vars({self.keys!r}{_args(kind=self.kind, bounds=self.bounds, lead=True)})"


class Constraints:
    """One constraint per key: ``model.cap = so.Constraints(plants, lambda p: ...)``.

    ``rule(key)`` returns each constraint, named ``name[key]``. Reading
    ``model.cap`` gives a dict from key to ConstraintRef.
    """

    __slots__ = ("keys", "rule")

    def __init__(self, keys: Iterable[Any], rule: Callable[[Any], Any]) -> None:
        self.keys = keys
        self.rule = rule

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_constraints(name, self.keys, self.rule)

    def __repr__(self) -> str:
        return f"Constraints({self.keys!r}, {self.rule!r})"


class Scaled:
    """A child block whose objective is scaled: ``model.south = so.Scaled(block, 1.2)``.

    Assigning a plain ``Block`` attaches it with a multiplier of 1.
    """

    __slots__ = ("block", "multiplier")

    def __init__(self, block: Any, multiplier: float) -> None:
        self.block = block
        self.multiplier = multiplier

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_block(name, self.block, self.multiplier)

    def __repr__(self) -> str:
        return f"Scaled({self.block!r}, {self.multiplier!r})"


class Blocks:
    """One child block per key: ``model.region = so.Blocks(regions, build_region)``.

    ``rule(key)`` returns each standalone Block, named ``name[key]``.
    ``multiplier`` scales each child's objective and may be a callable
    taking the key. Reading ``model.region`` gives a dict from key to
    child block.
    """

    __slots__ = ("keys", "rule", "multiplier")

    def __init__(self, keys: Iterable[Any], rule: Callable[[Any], Any], multiplier: Any = 1.0) -> None:
        self.keys = keys
        self.rule = rule
        self.multiplier = multiplier

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_blocks(name, self.keys, self.rule, self.multiplier)

    def __repr__(self) -> str:
        return f"Blocks({self.keys!r}, {self.rule!r}, multiplier={self.multiplier!r})"


class PiecewiseLinear:
    """A piecewise linear function of ``x``: ``model.cost = so.PiecewiseLinear(x, points, values)``.

    The arguments are ``add_piecewise``'s. Reading ``model.cost`` gives
    the Piecewise, so ``model.cost.value`` is f(x).
    """

    __slots__ = ("x", "points", "values", "method")

    def __init__(self, x: Any, points: Any, values: Any, *, method: Any = None) -> None:
        self.x = x
        self.points = points
        self.values = values
        self.method = method

    def _declare(self, owner: Any, name: str) -> None:
        owner.add_piecewise(name, self.x, self.points, self.values, **_given(method=self.method))

    def __repr__(self) -> str:
        method = _args(method=self.method, lead=True)
        return f"PiecewiseLinear({self.x!r}, {self.points!r}, {self.values!r}{method})"


_DECLARATIONS = (Var, Vars, Constraints, Scaled, Blocks, PiecewiseLinear)


def set_attribute(owner: Any, name: str, value: Any) -> None:
    """Declare ``value`` in ``owner`` (a Model or Block) under ``name``."""
    if name.startswith("_"):
        raise AttributeError(f"can't declare {name!r} by attribute: names starting with '_' are reserved")
    if hasattr(type(owner), name):
        raise AttributeError(
            f"can't declare {name!r} by attribute: it's the name of a {type(owner).__name__} method; "
            f"use the add_* methods to give something that name"
        )
    taken = owner._declared_kinds(name)
    if taken:
        raise AttributeError(f"{name!r} is already a {taken[0]} in this block")

    if isinstance(value, _DECLARATIONS):
        value._declare(owner, name)
    elif isinstance(value, _saiopt.Constraint):
        owner.add_constraint(name, value)
    elif isinstance(value, _saiopt.Block):
        owner.add_block(name, value)
    else:
        hint = " -- to declare a variable, assign so.Var(...)" if isinstance(value, _saiopt.Variable) else ""
        raise TypeError(
            f"can't declare {name!r} from a value of type {type(value).__name__}{hint}. Assign so.Var, "
            f"so.Vars, a constraint, so.Constraints, a Block, so.Scaled, so.Blocks, or so.PiecewiseLinear"
        )


def _given(**kwargs: Any) -> dict:
    """Only the arguments that were given, so the add_* defaults apply."""
    return {k: v for k, v in kwargs.items() if v is not None}


def _args(lead: bool = False, **kwargs: Any) -> str:
    text = ", ".join(f"{k}={v!r}" for k, v in _given(**kwargs).items())
    return f", {text}" if lead and text else text
