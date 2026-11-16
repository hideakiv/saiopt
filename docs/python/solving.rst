Solving and reading results
===========================

This page covers choosing a backend, passing solver options, checking
how a solve ended, and reading everything a ``SolveResult`` holds. The
examples use the diet model from :doc:`first-model`:

.. code-block:: python

   import saiopt as so

   model = so.Model()
   model.bread = so.Var(bounds=(0, so.inf))
   model.milk = so.Var(bounds=(0, so.inf))
   model.protein = so.Constraint(4 * model.bread + 8 * model.milk >= 20)
   model.calories = so.Constraint(100 * model.bread + 150 * model.milk >= 400)
   model.set_objective(so.ObjectiveSense.Minimize, 2 * model.bread + 3 * model.milk)

   bread, milk, calories = model.bread, model.milk, model.calories

Choosing a backend
------------------

``so.create_solver(model, name)`` builds a ``Solver`` for the model with
the backend registered under ``name``. Two backends ship with saiopt:

``"highs"`` (the default)
   `HiGHS <https://highs.dev/>`_: LP, MILP, and convex continuous QP.

``"scip"``
   `SCIP <https://scipopt.org/>`_, with SoPlex as its LP solver: LP,
   MILP, and any quadratic objective, solved to global optimality.

An unknown name raises ``RuntimeError``.

``create_solver`` translates the model into the backend's own form once.
After that you can call ``solve()`` as many times as you like on the same
``Solver`` without that cost. :doc:`modifying` builds on this.

Solver options
--------------

The optional third argument is a dict of backend options. saiopt passes
them to the backend unchanged, so they use the backend's own option
names. Each value must be a ``bool``, ``int``, ``float``, or ``str``.

.. code-block:: python

   solver = so.create_solver(model, "highs", {"time_limit": 30.0, "output_flag": False})

SCIP uses its own parameter names:

.. code-block:: python

   scip_solver = so.create_solver(model, "scip", {"limits/time": 30.0, "display/verblevel": 0})

An option the backend doesn't recognize, or a value it rejects, raises
``RuntimeError`` from ``create_solver``. See the
`HiGHS options <https://ergo-code.github.io/HiGHS/stable/options/definitions/>`_
and `SCIP parameters <https://www.scipopt.org/doc/html/PARAMETERS.php>`_
for the full lists.

How did the solve end?
----------------------

``result.status`` is a ``SolveStatus``. Each backend maps its own status
onto one of these:

* ``Optimal``
* ``Infeasible``, ``Unbounded``, ``InfeasibleOrUnbounded``
* a limit: ``TimeLimit``, ``IterationLimit``, ``NodeLimit``,
  ``SolutionLimit``, ``GapLimit``, ``ObjectiveLimit``, ``MemoryLimit``
* ``Interrupted`` (stopped by a progress callback, see below)
* ``Error``, ``Unknown``

A solve that stops early can still have found a feasible solution, such
as a MIP that hits its time limit with an incumbent. So whether there is
a solution to read is a separate flag, ``result.has_solution``. Check
both:

.. code-block:: python

   result = solver.solve()
   if result.optimal:
       print("optimal:", result.objective_value)
   elif result.has_solution:
       print("feasible, not proven optimal:", result.status, result.objective_value)
   else:
       print("no solution:", result.status)

.. code-block:: text

   optimal: 8.0

``objective_value`` and the variable values only mean something when
``has_solution`` is true.

Variable values
---------------

There are three ways to read values:

.. code-block:: python

   print(result[bread])                  # one variable
   print(result.value(milk))             # the same thing, as a method
   print(result.values([bread, milk]))   # a list, in the same order

.. code-block:: text

   1.0
   2.0
   [1.0, 2.0]

Pass ``values`` a dict, such as a group like ``model.ship``, and you get a
dict with the same keys back. ``result.variable_values`` holds every
value as a dict from ``Variable`` to number, but builds a new dict each
time you access it, so prefer the methods above for single lookups.

Duals and reduced costs
-----------------------

For an LP, the result also carries the dual value (shadow price) of each
constraint and the reduced cost of each variable:

.. code-block:: python

   print(result.dual(calories))
   print(result.reduced_cost(bread))

.. code-block:: text

   0.02
   0.0

Each extra calorie required would raise the cost by 0.02. A MILP has no
duals, so these dicts are empty for one.

Sensitivity ranging
-------------------

Sensitivity ranging tells you how far a coefficient or bound can move
before the optimal basis changes. It's off by default because it takes a
noticeable share of each solve on large models. Turn it on before
solving:

.. code-block:: python

   solver.set_sensitivity_ranging(True)
   result = solver.solve()
   print(result.objective_coefficient_range(bread))

.. code-block:: text

   SensitivityRange(1.5, 2)

Bread's cost can move between 1.5 and 2 with the same plan still
optimal. ``variable_bound_range(var)`` and ``constraint_bound_range(c)``
work the same way. Each returns a ``SensitivityRange`` with ``lower`` and
``upper`` attributes. Without ranging turned on, they raise
``KeyError``.

Watching progress
-----------------

``solver.set_progress_callback(callback)`` calls ``callback(progress)``
periodically during ``solve()`` with a ``SolverProgress``:
``elapsed_seconds``, ``iterations``, ``objective_value``, and, for a MIP
only, ``best_bound``, ``gap``, and ``node_count`` (``None`` otherwise).

Return ``False`` to stop the solve early. ``None`` or ``True`` keeps it
going. An exception raised in the callback also stops the solve, and is
re-raised from ``solve()``.

.. code-block:: python

   def report(progress):
       print(f"t={progress.elapsed_seconds:.1f}s obj={progress.objective_value:.2f}")
       return progress.elapsed_seconds < 30.0   # stop after 30 seconds

   solver.set_progress_callback(report)
   result = solver.solve()            # status is Interrupted if stopped early
   solver.set_progress_callback(None)

``solve()`` releases the GIL while the backend runs, so other Python
threads keep running during a long solve.

.. note::

   Once a HiGHS solve has been interrupted, that ``Solver`` keeps
   reporting the interrupt on every later ``solve()``. Build a new one
   with ``create_solver`` to try again. SCIP doesn't have this problem.

Writing the model to a file
---------------------------

``solver.write_model(path)`` writes the solver's current model, in the
format the file extension names. Both backends write ``.lp`` (CPLEX LP)
and ``.mps``; SCIP also writes its other formats, such as ``.cip``.
``path`` can be a string or a ``pathlib.Path``.

.. code-block:: python

   solver.write_model("diet.lp")

.. check: contents diet.lp

.. code-block:: text

   \ File written by HiGHS .lp file handler
   min
    obj: +2 bread +3 milk
   st
    protein: +4 bread +8 milk >= +20
    calories: +100 bread +150 milk >= +400
   bounds
   end

Names are rewritten into identifiers the file formats accept. For
example, ``ship[(W1, C1)]`` is written as ``ship((W1,C1))``.

Next, :doc:`modifying` changes a model through its ``Solver`` and
solves it again.
