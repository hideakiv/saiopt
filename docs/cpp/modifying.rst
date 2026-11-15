Changing a model and solving again
==================================

A ``Solver`` holds its own copy of the model in the backend. You can
read and change coefficients, bounds, and variable kinds on that copy
directly. ``HighsSolver``, for example, calls straight into HiGHS's own
change functions. The next ``solve()`` picks up the change without
rebuilding anything. This is the fast way to solve many variations of
one model: scenarios, parameter sweeps, or iterative algorithms.

The changes go to the ``Solver`` only. The ``Model`` you built it from
stays as it was, so a ``Solver`` created from it later starts from the
original. ``examples/solver_mutation.cpp`` walks through every setter.

Step 1: build and solve once
----------------------------

.. code-block:: cpp

   Model model;
   SAIOPT_VAR(model, x, .bounds = {0.0, 6.0});
   SAIOPT_VAR(model, y, .bounds = {0.0, kInfinity});
   SAIOPT_CONSTRAINT(model, demand, x + y >= 10.0);
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * x + 3.0 * y);

   auto solver = create_solver(model, "highs", SolverOptions{}.set_bool("output_flag", false));
   auto show = [&](const SolveResult& r) {
       std::printf("%g %g %g\n", r.objective_value, r.value(x), r.value(y));
   };
   show(solver->solve());

.. code-block:: text

   24 6 4

Step 2: change the objective
----------------------------

.. code-block:: cpp

   solver->set_objective_coefficient(x, 5.0);
   solver->set_objective_coefficient(y, 1.0);
   show(solver->solve());

.. code-block:: text

   10 0 10

Step 3: change bounds
---------------------

.. code-block:: cpp

   solver->set_variable_upper(y, 4.0);        // cap y
   solver->set_constraint_lower(demand, 8.0); // need less in total
   show(solver->solve());

.. code-block:: text

   24 4 4

Step 4: change a constraint coefficient
---------------------------------------

.. code-block:: cpp

   solver->set_constraint_coefficient(demand, y, 1.5);   // each unit of y now counts 1.5
   show(solver->solve());

.. code-block:: text

   14 2 4

Step 5: make a variable integer
-------------------------------

``set_variable_kind`` turns an LP into a MILP, or back. A MILP has no
duals, so ``result.duals`` comes back empty while any variable is
integer.

.. code-block:: cpp

   solver->set_variable_kind(x, VarKind::Integer);
   std::printf("%s\n", solver->variable_kind(x) == VarKind::Integer ? "integer" : "continuous");

.. code-block:: text

   integer

Every setter has a matching getter that reads the solver's current
value:

.. list-table::
   :header-rows: 1

   * - Getter
     - Setter
   * - ``objective_coefficient(v)``
     - ``set_objective_coefficient(v, c)``
   * - ``constraint_coefficient(c, v)``
     - ``set_constraint_coefficient(c, v, a)``
   * - ``constraint_lower(c)``
     - ``set_constraint_lower(c, lo)``
   * - ``constraint_upper(c)``
     - ``set_constraint_upper(c, hi)``
   * - ``variable_lower(v)``
     - ``set_variable_lower(v, lo)``
   * - ``variable_upper(v)``
     - ``set_variable_upper(v, hi)``
   * - ``variable_kind(v)``
     - ``set_variable_kind(v, kind)``

For a quadratic objective, ``objective_coefficient`` and its setter act
on the linear part only. The quadratic terms can't be changed through a
``Solver``. Passing a variable or constraint that isn't in the solver's
model throws ``std::runtime_error``.

``solver->write_model(path)`` writes the model with all these changes
applied.

Starting from a previous solution
---------------------------------

``solver->set_initial_solution(values)`` gives the next ``solve()`` a
starting point (a MIP start): an
``std::unordered_map<Variable, double>`` with values for some or all
variables. The backend tries to complete it into a feasible solution
and starts its search from there.

Passing a previous result's values back is the common case, for example
after a change that leaves the old solution feasible:

.. code-block:: cpp

   SolveResult first = solver->solve();
   solver->set_objective_coefficient(x, 4.0);
   solver->set_initial_solution(first.variable_values);
   SolveResult second = solver->solve();

The start is only a hint. One that can't be completed into a feasible
solution is dropped without an error. A value that isn't finite or lies
outside its variable's current bounds, or a variable from a different
model, throws ``std::runtime_error``. Calling ``set_initial_solution``
again adds another start rather than replacing the previous one.

Next, :doc:`mpi` builds blocks in parallel across processes.
