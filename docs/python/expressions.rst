Expressions and constraints
===========================

This page covers what goes inside ``add_constraint`` and
``set_objective``: linear expressions, constraints, and quadratic
objectives.

Variables
---------

``add_var`` takes a name and, optionally, a ``kind`` and ``bounds``, both
keyword-only:

.. code-block:: python

   import saiopt as so

   model = so.Model()
   x = model.add_var("x")                                   # continuous, unbounded
   y = model.add_var("y", bounds=(0, 10))                   # continuous, 0 <= y <= 10
   n = model.add_var("n", kind=so.VarKind.Integer, bounds=(0, 5))
   flag = model.add_var("flag", kind=so.VarKind.Binary)     # always [0, 1]

``bounds`` is a ``(lower, upper)`` pair. Use ``so.inf`` or ``-so.inf``
for a side with no bound; ``math.inf`` works too. A ``Binary`` variable
always has bounds ``[0, 1]``, whatever you pass.

Adding a second variable with a name that already exists in the same
model raises ``RuntimeError``.

Linear expressions
------------------

Adding, subtracting, negating, multiplying by a number, and dividing by a
number all build a ``LinearExpression``:

.. code-block:: python

   e = 3 * x - y / 2 + 4
   print(e.coefficient(x), e.coefficient(y), e.constant)

.. code-block:: text

   3.0 -0.5 4.0

``e.terms`` gives every coefficient as a dict from variable to number.

Summing many terms
~~~~~~~~~~~~~~~~~~

To add up a long sequence, use ``so.quicksum`` rather than the built-in
``sum``. ``sum`` builds a new expression on every addition, so it gets
slow on large models. ``quicksum`` adds in place:

.. code-block:: python

   items = [model.add_var(f"item{i}", bounds=(0, 1)) for i in range(100)]
   weights = [i % 7 + 1 for i in range(100)]

   total_weight = so.quicksum(w * v for w, v in zip(weights, items))

``+=`` on a ``LinearExpression`` also adds in place, which is handy when
you build an expression up in a loop:

.. code-block:: python

   total = so.LinearExpression()
   for w, v in zip(weights, items):
       total += w * v

Constraints
-----------

Comparing two expressions (or an expression and a number) with ``<=``,
``>=``, or ``==`` builds a ``Constraint``. Variables and numbers can be
on either side:

.. code-block:: python

   model.add_constraint("budget", total_weight <= 120)
   model.add_constraint("balance", x + y == 2 * n)
   model.add_constraint("floor", 1 <= y)

Two-sided ranges
~~~~~~~~~~~~~~~~

For a constraint with both a lower and an upper bound, construct
``so.Constraint(expression, lower, upper)`` directly:

.. code-block:: python

   model.add_constraint("band", so.Constraint(x + y, 2, 8))   # 2 <= x + y <= 8

Don't chain comparisons. Python runs ``2 <= x + y <= 8`` as
``(2 <= x + y) and (x + y <= 8)``, which would silently drop one of the
bounds. saiopt raises a ``TypeError`` instead.

Comparing variables
~~~~~~~~~~~~~~~~~~~

Because ``x == y`` builds a constraint, use ``x.is_same(y)`` to check
whether two handles refer to the same variable. Variables still work as
dict keys and in sets.

.. code-block:: python

   print(x.is_same(x), x.is_same(y))

.. code-block:: text

   True False

Quadratic objectives
--------------------

Multiplying two linear expressions, or squaring one with ``** 2``, gives
a ``QuadraticExpression``. Quadratic expressions can be objectives, but
not constraints: ``x * y <= 3`` raises ``TypeError``.

.. code-block:: python

   qp = so.Model()
   a = qp.add_var("a", bounds=(0, 10))
   b = qp.add_var("b", bounds=(0, 10))
   qp.add_constraint("total", a + b >= 4)
   qp.set_objective(so.ObjectiveSense.Minimize, (a - 1) ** 2 + (b - 2) ** 2 + a * b)

   result = so.create_solver(qp, "highs").solve()
   print(round(result.objective_value, 4), round(result[a], 4), round(result[b], 4))

.. code-block:: text

   4.0 1.0 3.0

The two backends handle quadratic objectives differently:

``"highs"``
   Solves a convex quadratic objective (concave when maximizing) with
   continuous variables only. It raises an error if the model has any
   integer variable. It doesn't check convexity, so a nonconvex
   objective can come back labeled optimal at a point that is only
   stationary.

``"scip"``
   Solves any quadratic objective to global optimality, with or without
   integer variables.

Next, :doc:`indexed` shows how to declare whole groups of variables and
constraints at once.
