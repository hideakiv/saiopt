Piecewise linear functions
==========================

Some costs and yields aren't linear but are made of straight segments: a
price that drops after the first 100 units, a tariff with tiers, a fee
charged only if anything is shipped at all. ``add_piecewise`` models
such a function from its breakpoints and gives back f(x) as a linear
expression, which you use anywhere an expression goes.

When the function is nonconvex, such as a cost that gets cheaper per
unit, an LP can't represent it. saiopt adds auxiliary variables so that
the solver picks the right segment. By default these are binary
variables, which every backend can solve. An SOS2 set is the
alternative, for backends that support one.

Step 1: the model
-----------------

A plant needs 180 units of material. Supplier A charges 5 per unit for
the first 100 units and 2 per unit after that, up to 250 units. Supplier
B charges a flat 4 per unit:

.. code-block:: python

   import saiopt as so

   model = so.Model()
   from_a = model.add_var("from_a", bounds=(0, so.inf))
   from_b = model.add_var("from_b", bounds=(0, so.inf))
   model.add_constraint("demand", from_a + from_b == 180)

Step 2: the piecewise cost
--------------------------

``add_piecewise(name, x, points, values)`` takes the breakpoints as two
lists: the x of each point and the function's value there. Between
breakpoints the function is a straight line:

.. code-block:: python

   cost_a = model.add_piecewise("cost_a", from_a, [0, 100, 250], [0, 500, 800])
   model.set_objective(so.ObjectiveSense.Minimize, cost_a.value + 4 * from_b)

``cost_a.value`` is the cost of ``from_a`` units from supplier A.
The model now has binary variables, so it's a MILP, which any backend
solves:

.. code-block:: python

   result = so.create_solver(model, "highs").solve()
   print(f"cost {result.objective_value:g}: {result[from_a]:g} from A")

.. code-block:: text

   cost 660: 180 from A

Buying everything from A pays off, because of the discount on units
past 100. A cheaper per-unit price for larger amounts makes the function
concave, so minimizing it is a nonconvex problem. That is why it needs
more than an LP.

``values`` can also be a function of the point, which saiopt calls once
per point:

.. code-block:: python

   tiers = model.add_piecewise("tiers", from_b, [0, 100, 200], lambda q: 4 * q)

Using an SOS2 set instead
-------------------------

Pass ``method=so.PiecewiseMethod.Sos2`` to model the same function with
an SOS2 set. This needs a backend that supports SOS2 sets, such as
``"scip"``. HiGHS doesn't support them, so ``create_solver(model,
"highs")`` raises ``RuntimeError`` for such a model.

.. code-block:: python

   sos_model = so.Model()
   a = sos_model.add_var("from_a", bounds=(0, so.inf))
   b = sos_model.add_var("from_b", bounds=(0, so.inf))
   sos_model.add_constraint("demand", a + b == 180)
   sos_cost = sos_model.add_piecewise("cost_a", a, [0, 100, 250], [0, 500, 800],
                                      method=so.PiecewiseMethod.Sos2)
   sos_model.set_objective(so.ObjectiveSense.Minimize, sos_cost.value + 4 * b)

   result = so.create_solver(sos_model, "scip").solve()
   print(f"cost {result.objective_value:g}: {result[a]:g} from A")

.. code-block:: text

   cost 660: 180 from A

Both give the same answer. Which one solves faster depends on the
model and the solver.

How it works
------------

``add_piecewise`` adds a child block named ``name`` that holds the
auxiliary variables. It also adds a constraint, also named ``name``, to
the block you called it on, tying those variables to ``x``.
``cost_a.block`` is that child block, and the auxiliary variables depend
on the method:

``PiecewiseMethod.Incremental`` (the default)
   A fraction ``delta[j]`` between 0 and 1 for how much of segment ``j``
   is used, and a binary ``full[j]`` for each segment but the last.
   Their constraints let a segment start only once the one before it is
   full. Then ``x`` is the first point plus each segment's width times
   its ``delta``, and f(x) is built the same way from ``values``.

``PiecewiseMethod.Sos2``
   A weight ``lambda[i]`` between 0 and 1 for each breakpoint. The
   weights add up to 1 (constraint ``convexity``), and an SOS2 set
   ``sos2`` lets at most two neighbouring weights be nonzero. Then ``x``
   is ``sum(lambda[i] * points[i])``, and f(x) is the same combination of
   ``values``.

.. code-block:: python

   print(len(cost_a.block.vars("delta")), len(cost_a.block.vars("full")))
   print(len(sos_cost.block.vars("lambda")))

.. code-block:: text

   2 1
   3

Either way, ``x`` can't go outside the first and last breakpoint. Here,
``from_a`` can't exceed 250. To leave ``x`` unrestricted, add
breakpoints far enough out that they never bind.

``add_piecewise`` checks its input before adding anything, and raises
``RuntimeError`` for any of these:

* ``points`` and ``values`` of different lengths, or fewer than two
  points;
* a value that isn't finite;
* ``points`` that decrease anywhere;
* a name already used by a child block or constraint.

Jumps
-----

The breakpoints must be in non-decreasing order, but a point may repeat.
A repeated point makes the function jump there: at that ``x`` it can take
any value between the two. A fee of 50 that's charged only when anything
is shipped looks like this:

.. code-block:: python

   model = so.Model()
   shipped = model.add_var("shipped", bounds=(10, so.inf))
   fee = model.add_piecewise("fee", shipped, [0, 0, 300], [0, 50, 350])
   model.set_objective(so.ObjectiveSense.Minimize, fee.value)
   print(so.create_solver(model, "highs").solve().objective_value)

.. code-block:: text

   60.0

Using f(x) in constraints
-------------------------

``value`` is an ordinary linear expression, so you can also constrain
another variable to it. ``y == f.value`` makes ``y`` equal to the
function. ``y <= f.value`` makes it at most the function, as in an
upper bound on a yield.

SOS2 sets directly
------------------

``add_sos2(name, vars)`` adds an SOS2 set over any variables of the
block or its descendants: at most two of them may be nonzero, and those
two must be neighbours in the order given. Like
``PiecewiseMethod.Sos2``, it needs the ``"scip"`` backend.

.. code-block:: python

   model = so.Model()
   v = model.add_vars("v", range(3), bounds=(0, 1))
   model.add_sos2("pick", [v[0], v[1], v[2]])
   model.set_objective(so.ObjectiveSense.Maximize, 2 * v[0] + 3 * v[1] + v[2])
   print(so.create_solver(model, "scip").solve().objective_value)

.. code-block:: text

   5.0

Next, :doc:`solving` covers solver options and everything a
``SolveResult`` holds.
