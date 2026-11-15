Piecewise linear functions
==========================

Some costs and yields aren't linear but are made of straight segments: a
price that drops after the first 100 units, a tariff with tiers, a fee
charged only if anything is shipped at all. ``SAIOPT_PIECEWISE`` models
such a function from its breakpoints and gives back f(x) as a
``LinearExpression``, which you use anywhere an expression goes.

When the function is nonconvex, such as a cost that gets cheaper per
unit, an LP can't represent it. saiopt then adds auxiliary variables,
together with either an SOS2 set or binary variables, so the solver picks
the right segment.

Step 1: the model
-----------------

A plant needs 180 units of material. Supplier A charges 5 per unit for
the first 100 units and 2 per unit after that, up to 250 units. Supplier
B charges a flat 4 per unit:

.. code-block:: cpp

   Model model;
   SAIOPT_VAR(model, from_a, .bounds = {0.0, kInfinity});
   SAIOPT_VAR(model, from_b, .bounds = {0.0, kInfinity});
   SAIOPT_CONSTRAINT(model, demand, from_a + from_b == 180.0);

Step 2: the piecewise cost
--------------------------

``SAIOPT_PIECEWISE(block, name, x, points, values)`` takes the
breakpoints as two vectors: the x of each point and the function's value
there. Between breakpoints the function is a straight line. Pass named
vectors: a braced list's commas would split the macro's arguments.

.. code-block:: cpp

   std::vector<double> amounts{0.0, 100.0, 250.0};
   std::vector<double> costs{0.0, 500.0, 800.0};
   SAIOPT_PIECEWISE(model, cost_a, from_a, amounts, costs);
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, cost_a.value + 4.0 * from_b);

``cost_a`` is a ``Piecewise``, and ``cost_a.value`` is the cost of
``from_a`` units from supplier A. Solve with SCIP, because this model uses
an SOS2 set (see `How it works`_ below):

.. code-block:: cpp

   SolveResult result = create_solver(model, "scip")->solve();
   std::printf("cost %g: %g from A\n", result.objective_value, result.value(from_a));

.. code-block:: text

   cost 660: 180 from A

Buying everything from A pays off, because of the discount on units
past 100. A cheaper per-unit price for larger amounts makes the function
concave, so minimizing it is a nonconvex problem. That is why it needs
more than an LP.

``values`` can also be a function of the point, which saiopt calls once
per point. Wrap a lambda in parentheses, as for ``SAIOPT_CONSTRAINTS``:

.. code-block:: cpp

   std::vector<double> volumes{0.0, 100.0, 200.0};
   SAIOPT_PIECEWISE(model, tiers, from_b, volumes, ([](double q) { return 4.0 * q; }));

The functions behind the macro are ``Block::add_piecewise(name, x, points,
values)`` and an overload that takes the function instead of ``values``.
``x`` can be any ``LinearExpression``, not just a variable.

Solving with HiGHS
------------------

HiGHS doesn't support SOS2 sets, so ``create_solver(model, "highs")``
throws ``std::runtime_error`` for the model above. Pass
``PiecewiseMethod::Incremental`` as a last argument to build the same
function from binary variables instead. Every backend can solve that
form:

.. code-block:: cpp

   Model with_highs;
   SAIOPT_VAR(with_highs, from_a2, .bounds = {0.0, kInfinity});
   SAIOPT_VAR(with_highs, from_b2, .bounds = {0.0, kInfinity});
   SAIOPT_CONSTRAINT(with_highs, demand2, from_a2 + from_b2 == 180.0);
   SAIOPT_PIECEWISE(with_highs, cost_a2, from_a2, amounts, costs, PiecewiseMethod::Incremental);
   SAIOPT_OBJECTIVE(with_highs, ObjectiveSense::Minimize, cost_a2.value + 4.0 * from_b2);

   SolveResult highs_result = create_solver(with_highs, "highs")->solve();
   std::printf("cost %g: %g from A\n", highs_result.objective_value, highs_result.value(from_a2));

.. code-block:: text

   cost 660: 180 from A

How it works
------------

``add_piecewise`` adds a child block named ``name`` that holds the
auxiliary variables. It also adds a constraint, also named ``name``, to
the block you called it on, tying those variables to ``x``.
``cost_a.block`` is the child's ``BlockRef``, and the auxiliary variables
depend on the method:

``PiecewiseMethod::Sos2`` (the default)
   A weight ``lambda[i]`` between 0 and 1 for each breakpoint. The
   weights add up to 1 (constraint ``convexity``), and an SOS2 set
   ``sos2`` lets at most two neighbouring weights be nonzero. Then ``x``
   is the sum of ``lambda[i] * points[i]``, and f(x) is the same
   combination of ``values``.

``PiecewiseMethod::Incremental``
   A fraction ``delta[j]`` between 0 and 1 for how much of segment ``j``
   is used, and a binary ``full[j]`` for each segment but the last.
   Their constraints let a segment start only once the one before it is
   full.

.. code-block:: cpp

   const Block& aux = with_highs.block(cost_a2.block);
   std::printf("%zu %zu\n", aux.vars("delta").size(), aux.vars("full").size());

.. code-block:: text

   2 1

Either way, ``x`` can't go outside the first and last breakpoint. Here,
``from_a`` can't exceed 250. To leave ``x`` unrestricted, add
breakpoints far enough out that they never bind.

``add_piecewise`` checks its input before adding anything, and throws
``std::runtime_error`` for any of these:

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

.. code-block:: cpp

   Model shipping;
   SAIOPT_VAR(shipping, shipped, .bounds = {10.0, kInfinity});
   std::vector<double> loads{0.0, 0.0, 300.0};
   std::vector<double> charges{0.0, 50.0, 350.0};
   SAIOPT_PIECEWISE(shipping, fee, shipped, loads, charges);
   SAIOPT_OBJECTIVE(shipping, ObjectiveSense::Minimize, fee.value);
   std::printf("%g\n", create_solver(shipping, "scip")->solve().objective_value);

.. code-block:: text

   60

Using f(x) in constraints
-------------------------

``value`` is an ordinary linear expression, so you can also constrain
another variable to it. ``y == f.value`` makes ``y`` equal to the
function. ``y <= f.value`` makes it at most the function, as in an
upper bound on a yield.

SOS2 sets directly
------------------

``Block::add_sos2(name, vars)`` adds an SOS2 set over any variables of
the block or its descendants: at most two of them may be nonzero, and
those two must be neighbours in the order given. Like ``add_piecewise``
with the default method, it needs the ``"scip"`` backend. SOS2 sets go
along when a block is serialized, so they work with :doc:`mpi` too.

.. code-block:: cpp

   Model pick_two;
   std::vector<int> slots{0, 1, 2};
   SAIOPT_VARS(pick_two, v, slots, .bounds = {0.0, 1.0});
   pick_two.add_sos2("pick", {v[0], v[1], v[2]});
   SAIOPT_OBJECTIVE(pick_two, ObjectiveSense::Maximize, 2.0 * v[0] + 3.0 * v[1] + v[2]);
   std::printf("%g\n", create_solver(pick_two, "scip")->solve().objective_value);

.. code-block:: text

   5

Next, :doc:`solving` covers solver options and everything a
``SolveResult`` holds.
