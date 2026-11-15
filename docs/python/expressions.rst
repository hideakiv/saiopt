Expressions and constraints
===========================

This page covers what goes into constraints and objectives: linear
expressions, constraints, and quadratic objectives.

Variables
---------

``so.Var`` optionally takes a ``kind`` and ``bounds``, both
keyword-only:

.. code-block:: python

   import saiopt as so

   model = so.Model()
   model.x = so.Var()                                       # continuous, unbounded
   model.y = so.Var(bounds=(0, 10))                         # continuous, 0 <= y <= 10
   model.n = so.Var(kind=so.VarKind.Integer, bounds=(0, 5))
   model.flag = so.Var(kind=so.VarKind.Binary)              # always [0, 1]

``bounds`` is a ``(lower, upper)`` pair. Use ``so.inf`` or ``-so.inf``
for a side with no bound; ``math.inf`` works too. A ``Binary`` variable
always has bounds ``[0, 1]``, whatever you pass.

Declaring a second item under a name that already exists in the same
model raises ``AttributeError``.

Reading ``model.x`` gives the variable itself, so when an expression uses
the same variables many times you can keep them in local names:

.. code-block:: python

   x, y, n = model.x, model.y, model.n

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

   model.item = so.Vars(range(100), bounds=(0, 1))   # 100 variables; see the next page
   weights = [i % 7 + 1 for i in range(100)]

   total_weight = so.quicksum(w * model.item[i] for i, w in enumerate(weights))

``+=`` on a ``LinearExpression`` also adds in place, which is handy when
you build an expression up in a loop:

.. code-block:: python

   total = so.LinearExpression()
   for i, w in enumerate(weights):
       total += w * model.item[i]

Constraints
-----------

Comparing two expressions (or an expression and a number) with ``<=``,
``>=``, or ``==`` builds a ``Constraint``. Variables and numbers can be
on either side:

.. code-block:: python

   model.budget = total_weight <= 120
   model.balance = x + y == 2 * n
   model.floor = 1 <= y

Two-sided ranges
~~~~~~~~~~~~~~~~

For a constraint with both a lower and an upper bound, construct
``so.Constraint(expression, lower, upper)`` directly:

.. code-block:: python

   model.band = so.Constraint(x + y, 2, 8)   # 2 <= x + y <= 8

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
   qp.a = so.Var(bounds=(0, 10))
   qp.b = so.Var(bounds=(0, 10))
   qp.total = qp.a + qp.b >= 4
   qp.set_objective(so.ObjectiveSense.Minimize, (qp.a - 1) ** 2 + (qp.b - 2) ** 2 + qp.a * qp.b)

   result = so.create_solver(qp, "highs").solve()
   print(round(result.objective_value, 4), round(result[qp.a], 4), round(result[qp.b], 4))

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
