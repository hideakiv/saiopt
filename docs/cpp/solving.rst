Solving and reading results
===========================

This page covers choosing a backend, passing solver options, checking
how a solve ended, and reading everything a ``SolveResult`` holds. The
examples use the diet model from :doc:`first-model`:

.. code-block:: cpp

   Model model;
   SAIOPT_VAR(model, bread, .bounds = {0.0, kInfinity});
   SAIOPT_VAR(model, milk, .bounds = {0.0, kInfinity});
   SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
   SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);

Choosing a backend
------------------

``create_solver(model, name)`` builds a ``Solver`` for the model with
the backend registered under ``name``. Two backends ship with saiopt,
each in its own library:

``"highs"`` (``saiopt_highs``)
   `HiGHS <https://highs.dev/>`_: LP, MILP, and convex continuous QP.

``"scip"`` (``saiopt_scip``)
   `SCIP <https://scipopt.org/>`_, with SoPlex as its LP solver: LP,
   MILP, and any quadratic objective, solved to global optimality.

A backend is available once its library is linked in (see
:doc:`installation`). An unknown name throws ``std::runtime_error``.

``create_solver`` translates the model into the backend's own form once.
After that you can call ``solve()`` as many times as you like on the same
``Solver`` without that cost. :doc:`modifying` builds on this.

Solver options
--------------

The optional third argument is a ``SolverOptions``, with one setter per
value type: ``set_bool``, ``set_int``, ``set_double``, and
``set_string``. The setters chain. saiopt passes the options to the
backend unchanged, so they use the backend's own option names:

.. code-block:: cpp

   auto solver = create_solver(model, "highs",
       SolverOptions{}.set_double("time_limit", 30.0).set_bool("output_flag", false));

SCIP uses its own parameter names:

.. code-block:: cpp

   auto scip_solver = create_solver(model, "scip",
       SolverOptions{}.set_double("limits/time", 30.0).set_int("display/verblevel", 0));

For SCIP, ``set_int`` works for both its ``int`` and ``Longint``
parameters.

An option the backend doesn't recognize, or a value it rejects, makes
``create_solver`` throw ``std::runtime_error``. See the
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

``to_string(status)`` gives a readable name.

A solve that stops early can still have found a feasible solution, such
as a MIP that hits its time limit with an incumbent. So whether there is
a solution to read is a separate flag, ``result.has_solution``. Check
both:

.. code-block:: cpp

   SolveResult result = solver->solve();
   if (result.optimal()) {
       std::printf("optimal: %g\n", result.objective_value);
   } else if (result.has_solution) {
       std::printf("feasible, not proven optimal: %g\n", result.objective_value);
   } else {
       std::printf("no solution: %s\n", std::string(to_string(result.status)).c_str());
   }

.. code-block:: text

   optimal: 8

``objective_value`` and the variable values are only filled in when
``has_solution`` is true.

Variable values
---------------

``result.value(var)`` returns one variable's value:

.. code-block:: cpp

   std::printf("%g %g\n", result.value(bread), result.value(milk));

.. code-block:: text

   1 2

Every value is also in ``result.variable_values``, a
``std::unordered_map<Variable, double>``. ``value`` throws
``std::out_of_range`` if there's no value for the variable.

Duals and reduced costs
-----------------------

For an LP, the result also carries the dual value (shadow price) of each
constraint and the reduced cost of each variable:

.. code-block:: cpp

   std::printf("%g %g\n", result.dual(calories), result.reduced_cost(bread));

.. code-block:: text

   0.02 0

Each extra calorie required would raise the cost by 0.02. These are also
available as the maps ``result.duals`` and ``result.reduced_costs``. A
MILP has no duals, so both maps are empty for one.

Sensitivity ranging
-------------------

Sensitivity ranging tells you how far a coefficient or bound can move
before the optimal basis changes. It's off by default because it takes a
noticeable share of each solve on large models. Turn it on before
solving:

.. code-block:: cpp

   solver->set_sensitivity_ranging(true);
   SolveResult ranged = solver->solve();
   const SensitivityRange& range = ranged.objective_coefficient_range(bread);
   std::printf("[%g, %g]\n", range.lower, range.upper);

.. code-block:: text

   [1.5, 2]

Bread's cost can move between 1.5 and 2 with the same plan still
optimal. ``variable_bound_range(var)`` and ``constraint_bound_range(c)``
work the same way, as do the maps ``objective_coefficient_ranges``,
``variable_bound_ranges``, and ``constraint_bound_ranges``.

Watching progress
-----------------

``solver->set_progress_callback(callback)`` calls ``callback`` periodically
during ``solve()`` with a ``SolverProgress``: ``elapsed_seconds``,
``iterations``, ``objective_value``, and, for a MIP only, ``best_bound``,
``gap``, and ``node_count`` as ``std::optional`` values, empty otherwise.

Return ``false`` from the callback to stop the solve early:

.. code-block:: cpp

   solver->set_progress_callback([](const SolverProgress& p) {
       std::printf("t=%.1fs obj=%.2f\n", p.elapsed_seconds, p.objective_value);
       return p.elapsed_seconds < 30.0;   // stop after 30 seconds
   });
   SolveResult watched = solver->solve();   // status is Interrupted if stopped early

.. note::

   Once a HiGHS solve has been interrupted, that ``Solver`` keeps
   reporting the interrupt on every later ``solve()``. Build a new one
   with ``create_solver`` to try again. SCIP doesn't have this problem.

Writing the model to a file
---------------------------

``solver->write_model(path)`` writes the solver's current model, in the
format the file extension names. Both backends write ``.lp`` (CPLEX LP)
and ``.mps``; SCIP also writes its other formats, such as ``.cip``. An
unsupported extension throws.

.. code-block:: cpp

   solver->write_model("diet.lp");

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
