Changing a model and solving again
==================================

A ``Solver`` holds its own copy of the model in the backend. You can
read and change coefficients, bounds, and variable kinds on that copy
directly, and the next ``solve()`` picks up the change without rebuilding
anything. This is the fast way to solve many variations of one model:
scenarios, parameter sweeps, or iterative algorithms.

The changes go to the ``Solver`` only. The ``Model`` you built it from
stays as it was, so a ``Solver`` created from it later starts from the
original.

Step 1: build and solve once
----------------------------

.. code-block:: python

   import saiopt as so

   model = so.Model()
   model.x = so.Var(bounds=(0, 6))
   model.y = so.Var(bounds=(0, so.inf))
   model.demand = so.Constraint(model.x + model.y >= 10)
   model.set_objective(so.ObjectiveSense.Minimize, 2 * model.x + 3 * model.y)

   x, y, demand = model.x, model.y, model.demand

   solver = so.create_solver(model, "highs", {"output_flag": False})
   r = solver.solve()
   print(r.objective_value, r[x], r[y])

.. code-block:: text

   24.0 6.0 4.0

Step 2: change the objective
----------------------------

.. code-block:: python

   solver.set_objective_coefficient(x, 5)
   solver.set_objective_coefficient(y, 1)
   r = solver.solve()
   print(r.objective_value, r[x], r[y])

.. code-block:: text

   10.0 0.0 10.0

Step 3: change bounds
---------------------

.. code-block:: python

   solver.set_variable_upper(y, 4)     # cap y
   solver.set_constraint_lower(demand, 8)   # need less in total
   r = solver.solve()
   print(r.objective_value, r[x], r[y])

.. code-block:: text

   24.0 4.0 4.0

Step 4: change a constraint coefficient
---------------------------------------

.. code-block:: python

   solver.set_constraint_coefficient(demand, y, 1.5)   # each unit of y now counts 1.5
   r = solver.solve()
   print(r.objective_value, r[x], r[y])

.. code-block:: text

   14.0 2.0 4.0

Step 5: make a variable integer
-------------------------------

``set_variable_kind`` turns an LP into a MILP, or back:

.. code-block:: python

   solver.set_variable_kind(x, so.VarKind.Integer)
   print(solver.variable_kind(x))

.. code-block:: text

   VarKind.Integer

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

``solver.write_model(path)`` writes the model with all these changes
applied.

Starting from a previous solution
---------------------------------

``solver.set_initial_solution(values)`` gives the next ``solve()`` a
starting point (a MIP start): a dict from variable to value, for some or
all variables. The backend tries to complete it into a feasible solution
and starts its search from there.

Passing a previous result's values back is the common case, for example
after a change that leaves the old solution feasible:

.. code-block:: python

   first = solver.solve()
   solver.set_objective_coefficient(x, 4)
   solver.set_initial_solution(first.variable_values)
   second = solver.solve()

The start is only a hint. One that can't be completed into a feasible
solution is dropped without an error. A value outside its variable's
bounds, or a variable from a different model, raises an error.

Next, :doc:`mpi` builds blocks in parallel across processes.
