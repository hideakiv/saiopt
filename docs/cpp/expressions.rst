Expressions and constraints
===========================

This page covers declaring variables, and what goes inside
``SAIOPT_CONSTRAINT`` and ``SAIOPT_OBJECTIVE``: linear expressions,
constraints, and quadratic objectives.

Variables
---------

``SAIOPT_VAR`` takes two optional, independent settings, written as C++20
designated initializers: ``.kind`` and ``.bounds``.

.. code-block:: cpp

   Model model;
   SAIOPT_VAR(model, x);                                               // continuous, unbounded
   SAIOPT_VAR(model, y, .bounds = {0.0, 10.0});                        // continuous, 0 <= y <= 10
   SAIOPT_VAR(model, n, .kind = VarKind::Integer, .bounds = {0.0, 5.0});
   SAIOPT_VAR(model, flag, .kind = VarKind::Binary);                   // always [0, 1]

``.kind`` is ``VarKind::Continuous`` (the default), ``VarKind::Integer``,
or ``VarKind::Binary``. ``.bounds`` is a ``{lower, upper}`` pair; use
``kInfinity`` or ``-kInfinity`` for a side with no bound. A binary
variable always has bounds ``[0, 1]``. When you give both settings,
designated initializers require ``.kind`` before ``.bounds``.

Each macro declares a local ``saiopt::Variable`` named after the
identifier and registers the same name in the model. Adding a second
variable with a name that already exists in the same model throws
``std::runtime_error``.

``Variable`` is a small handle that is cheap to copy.

Linear expressions
------------------

Adding and subtracting variables, multiplying them by a ``double``, and
negating them all build a ``LinearExpression``:

.. code-block:: cpp

   LinearExpression e = 3.0 * x - 0.5 * y + 4.0;
   std::printf("%g %g %g\n", e.coefficient(x), e.coefficient(y), e.constant());

.. code-block:: text

   3 -0.5 4

There is no division operator, so multiply by the reciprocal instead.

To build an expression up in a loop, start from an empty
``LinearExpression`` and use ``+=``:

.. code-block:: cpp

   std::vector<int> ids{0, 1, 2};
   std::vector<double> weights{4.0, 2.0, 7.0};
   SAIOPT_VARS(model, item, ids, .kind = VarKind::Binary);   // item[0], item[1], item[2]

   LinearExpression total_weight;
   for (int i : ids) total_weight += weights[i] * item[i];

``SAIOPT_VARS`` declares one variable per key; :doc:`indexed` covers it
in detail.

Constraints
-----------

Comparing an expression with ``<=``, ``>=``, or ``==`` builds a
``Constraint``. Numbers can be on either side:

.. code-block:: cpp

   SAIOPT_CONSTRAINT(model, budget, total_weight <= 10.0);
   SAIOPT_CONSTRAINT(model, balance, x + y == 2.0 * n);
   SAIOPT_CONSTRAINT(model, floor, 1.0 <= y);

Each one declares a ``ConstraintRef`` handle named after the identifier.

Two-sided ranges
~~~~~~~~~~~~~~~~

A chained comparison builds one constraint with both bounds:

.. code-block:: cpp

   SAIOPT_CONSTRAINT(model, band, 2.0 <= x + y <= 8.0);

``Constraint(expression, lower, upper)`` builds the same thing
explicitly.

Comparing two variables
~~~~~~~~~~~~~~~~~~~~~~~

``==`` always builds a constraint, including between two plain
variables:

.. code-block:: cpp

   SAIOPT_CONSTRAINT(model, same, x == y);   // x = y

To check whether two ``Variable`` handles refer to the same variable,
use ``is_same``:

.. code-block:: cpp

   std::printf("%d %d\n", x.is_same(x), x.is_same(y));

.. code-block:: text

   1 0

``Variable`` has no ``!=``. ``Variable`` still works as a key in
``std::unordered_map`` and ``std::unordered_set``, which use
``is_same`` through saiopt's ``std::equal_to<Variable>``.

Quadratic objectives
--------------------

Multiplying two linear expressions gives a ``QuadraticExpression``.
Quadratic expressions can be objectives, on a model or on any block, but
constraints stay linear, so ``x * y <= 3.0`` doesn't compile.

.. code-block:: cpp

   Model qp;
   SAIOPT_VAR(qp, a, .bounds = {0.0, 10.0});
   SAIOPT_VAR(qp, b, .bounds = {0.0, 10.0});
   SAIOPT_CONSTRAINT(qp, total, a + b >= 4.0);
   SAIOPT_OBJECTIVE(qp, ObjectiveSense::Minimize, (a - 1.0) * (a - 1.0) + (b - 2.0) * (b - 2.0) + a * b);

   SolveResult qr = create_solver(qp, "highs")->solve();
   std::printf("%.4g %.4g %.4g\n", qr.objective_value, qr.value(a), qr.value(b));

.. code-block:: text

   4 1 3

The two backends handle quadratic objectives differently:

``"highs"``
   Solves a convex quadratic objective (concave when maximizing) with
   continuous variables only. It throws if the model has any integer
   variable. It doesn't check convexity, so a nonconvex objective can
   come back labeled optimal at a point that is only stationary.

``"scip"``
   Solves any quadratic objective to global optimality, with or without
   integer variables.

Next, :doc:`indexed` shows how to declare whole families of variables
and constraints at once.
