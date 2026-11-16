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

Assigning ``so.Var(...)`` to an attribute of the model adds a variable,
named after the attribute. Reading the attribute back, as
``model.bread``, gives you the variable. Variables are continuous and
unbounded by default, so give these a lower bound of 0 (you can't buy
negative bread). ``so.inf`` is saiopt's infinite bound.

.. code-block:: python

   model.bread = so.Var(bounds=(0, so.inf))
   model.milk = so.Var(bounds=(0, so.inf))

Every variable, constraint, and other item in a model has a name, unique
within it, so you can always look it up again later.

Step 3: add constraints
-----------------------

Arithmetic on variables builds expressions, and comparing an expression
with ``<=``, ``>=``, or ``==`` builds a constraint. Assigning
``so.Constraint(...)`` of one to an attribute adds it under that name, the
same way ``so.Var`` declares a variable:

.. code-block:: python

   model.protein = so.Constraint(4 * model.bread + 8 * model.milk >= 20)
   model.calories = so.Constraint(100 * model.bread + 150 * model.milk >= 400)

Step 4: set the objective
-------------------------

The objective is an expression plus a direction: minimize or maximize.

.. code-block:: python

   model.set_objective(so.ObjectiveSense.Minimize, 2 * model.bread + 3 * model.milk)

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
       print("bread:", result[model.bread])
       print("milk:", result[model.milk])
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
   model.bread = so.Var(bounds=(0, so.inf))
   model.milk = so.Var(bounds=(0, so.inf))
   model.protein = so.Constraint(4 * model.bread + 8 * model.milk >= 20)
   model.calories = so.Constraint(100 * model.bread + 150 * model.milk >= 400)
   model.set_objective(so.ObjectiveSense.Minimize, 2 * model.bread + 3 * model.milk)

   result = so.create_solver(model, "highs").solve()
   print(result.status, result.objective_value, result[model.bread], result[model.milk])

Names that aren't attributes
----------------------------

The attribute name becomes the item's name, so it has to be a valid
Python identifier. It also can't start with ``_`` or be the name of a
``Model`` method, such as ``vars``. For any other name, or one computed
while the program runs, use the ``add_*`` methods. They take the name
as a string and return the new item:

.. code-block:: python

   other = so.Model()
   for food in ["whole wheat", "rye"]:
       other.add_var(food, bounds=(0, so.inf))

   rye = other.var("rye")                    # look an item up by its name
   other.add_constraint("at least one", other.var("whole wheat") + rye >= 1)

A name can be any nonempty string without a ``.``, so spaces are fine.
saiopt joins block names with ``.`` into full names such as
``north.produce``, and rejects a ``.`` in a name you give so that full
names stay unambiguous. A key of an indexed group may still contain one,
as in ``x[2.5]``. ``add_*`` raises ``RuntimeError`` for a name that
breaks these rules, as it does for one already used in the block.

Each attribute declaration has an ``add_*`` counterpart: ``add_var``,
``add_vars``, ``add_constraint``, ``add_constraints``, ``add_block``,
``add_blocks``, and ``add_piecewise``. The two styles can be mixed in one
model. Within one model, a name declared through an attribute can't
also be used for a different kind of item.

Next, :doc:`expressions` covers what you can write inside constraints and
objectives.
