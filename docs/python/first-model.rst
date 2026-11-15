Your first model
================

This page builds and solves a small linear program from start to finish:
the classic *diet problem*. You buy bread and milk, and want to spend as
little as possible while getting at least 20 units of protein and 400
calories:

=========  ======  ========  ========
Food       Cost    Protein   Calories
=========  ======  ========  ========
bread      2       4         100
milk       3       8         150
=========  ======  ========  ========

Step 1: create a model
----------------------

A ``Model`` is the thing you solve. It starts out empty.

.. code-block:: python

   import saiopt as so

   model = so.Model()

Step 2: add variables
---------------------

``add_var`` adds one variable and returns a handle to it. Every variable
has a name, which must be unique within the model. Variables are
continuous and unbounded by default, so give these a lower bound of 0
(you can't buy negative bread). ``so.inf`` is saiopt's infinite bound.

.. code-block:: python

   bread = model.add_var("bread", bounds=(0, so.inf))
   milk = model.add_var("milk", bounds=(0, so.inf))

Step 3: add constraints
-----------------------

Arithmetic on variables builds expressions, and comparing an expression
with ``<=``, ``>=``, or ``==`` builds a constraint. ``add_constraint``
adds it to the model under a name:

.. code-block:: python

   model.add_constraint("protein", 4 * bread + 8 * milk >= 20)
   model.add_constraint("calories", 100 * bread + 150 * milk >= 400)

Step 4: set the objective
-------------------------

The objective is an expression plus a direction: minimize or maximize.

.. code-block:: python

   model.set_objective(so.ObjectiveSense.Minimize, 2 * bread + 3 * milk)

You can print a model to check what you've built:

.. code-block:: python

   print(model)

.. code-block:: text

   model (minimize)
   objective:
     2 bread + 3 milk
   variables:
     bread  continuous  [0, +inf]
     milk  continuous  [0, +inf]
   constraints:
     protein: 4 bread + 8 milk >= 20
     calories: 100 bread + 150 milk >= 400

Step 5: solve
-------------

``create_solver`` hands the model to a solver backend, here HiGHS, and
returns a ``Solver``. Calling ``solve()`` on it returns a
``SolveResult``:

.. code-block:: python

   solver = so.create_solver(model, "highs")
   result = solver.solve()

Step 6: read the solution
-------------------------

Check the status first: only a solve that found a solution has values to
read. Index the result with a variable to get its value.

.. code-block:: python

   if result.optimal:
       print("cost:", result.objective_value)
       print("bread:", result[bread])
       print("milk:", result[milk])
   else:
       print("no optimal solution:", result.status)

.. code-block:: text

   cost: 8.0
   bread: 1.0
   milk: 2.0

This model has more than one optimal solution (buying 8/3 units of milk
and no bread also costs 8), so another backend may report different
amounts for the same cost.

To solve with SCIP instead, change only the backend name:
``so.create_solver(model, "scip")``. Nothing else in the model changes.

The whole program
-----------------

.. code-block:: python

   import saiopt as so

   model = so.Model()
   bread = model.add_var("bread", bounds=(0, so.inf))
   milk = model.add_var("milk", bounds=(0, so.inf))
   model.add_constraint("protein", 4 * bread + 8 * milk >= 20)
   model.add_constraint("calories", 100 * bread + 150 * milk >= 400)
   model.set_objective(so.ObjectiveSense.Minimize, 2 * bread + 3 * milk)

   result = so.create_solver(model, "highs").solve()
   print(result.status, result.objective_value, result[bread], result[milk])

Next, :doc:`expressions` covers what you can write inside constraints and
objectives.
