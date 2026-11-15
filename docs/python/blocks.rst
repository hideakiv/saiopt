Composing models from blocks
============================

A ``Block`` is a sub-model with its own variables, constraints, and
objective contribution. You build one on its own, often in a function
that knows nothing about the rest of the model, and then attach it to a
parent. A ``Model`` is the root of this tree of blocks, and it's what
you solve.

Blocks help in three ways:

* Each part of a model can be written and tested on its own.
* Names are local to a block, so two blocks can both have a variable
  called ``produce`` without clashing.
* Blocks can be built in parallel, in separate processes (see
  :doc:`mpi`).

This page builds a small facility-location model. The root model decides
which facilities to open, and each facility's production plan is its own
block.

Step 1: a function that builds one block
----------------------------------------

Create a ``Block()`` and declare its contents the same way as on a
model. A block's objective has no direction of its own:
``set_objective`` takes just the expression, and the model's direction
applies to it.

.. code-block:: python

   import saiopt as so

   periods = range(3)

   def build_facility(period_capacity, unit_cost):
       block = so.Block()
       block.produce = so.Vars(periods, bounds=(0, period_capacity))
       block.total = so.Var(bounds=(0, so.inf))
       block.total_link = block.total == so.quicksum(block.produce.values())
       block.set_objective(unit_cost * block.total)
       return block

Step 2: attach blocks to a model
--------------------------------

Assigning a ``Block`` to an attribute moves it into the parent under
that name, and the block's objective is added to the parent's. To scale
the objective, assign ``so.Scaled(block, multiplier)`` instead:

.. code-block:: python

   model = so.Model()
   model.north = build_facility(period_capacity=40, unit_cost=2.0)
   model.south = so.Scaled(build_facility(period_capacity=30, unit_cost=1.5), 1.2)

Once attached, the original ``Block`` object can't be used any more: any
call on it raises ``RuntimeError``. Use ``model.north`` instead, which
reads back the attached child.

Step 3: reach into a child
--------------------------

A child's names live in the child, so reach its variables through it:

.. code-block:: python

   north_total = model.north.total
   south_total = model.south.total

Constraints can only point *down* the tree. A block's constraints and
objective may use its own variables and those of the blocks attached
below it, but never a parent's. The root can therefore tie the
facilities together:

.. code-block:: python

   model.is_open = so.Vars(["north", "south"], kind=so.VarKind.Binary)
   model.north_cap = north_total <= 100 * model.is_open["north"]
   model.south_cap = south_total <= 80 * model.is_open["south"]
   model.demand = north_total + south_total >= 90

A constraint inside ``north`` that used ``is_open`` would be rejected
with a ``RuntimeError`` when you create the solver.

Step 4: solve the whole tree
----------------------------

The root's objective is added to every block's contribution, each
scaled by its multiplier:

.. code-block:: python

   model.set_objective(so.ObjectiveSense.Minimize, 50 * model.is_open["north"] + 30 * model.is_open["south"])

   result = so.create_solver(model, "highs").solve()
   print("total cost:", round(result.objective_value, 2))
   print("north:", result[north_total], "south:", result[south_total])

.. code-block:: text

   total cost: 230.0
   north: 90.0 south: 0.0

The total adds the root's fixed costs (50 or 30 per open facility) to
each facility's production cost: ``2.0 * north_total`` for north and
``1.2 * 1.5 * south_total`` for south, scaled by its multiplier. South is
cheaper per unit, but its capacity of 80 can't cover the demand of 90 on
its own, so opening only north is cheapest.

Many blocks at once
-------------------

``so.Blocks(keys, rule, multiplier=1.0)`` attaches one block per key:
``rule(key)`` builds it, and it's named ``name[key]``. The multiplier
can also be a function of the key. Reading the attribute gives a dict
from key to child:

.. code-block:: python

   capacity = {"east": 25, "west": 35}
   cost = {"east": 1.0, "west": 1.1}

   regional = so.Model()
   regional.region = so.Blocks(
       capacity,
       lambda r: build_facility(capacity[r], cost[r]),
       multiplier=lambda r: 1.0 if r == "east" else 0.9,
   )
   totals = {r: region.total for r, region in regional.region.items()}
   regional.demand = so.quicksum(totals.values()) >= 100
   regional.set_objective(so.ObjectiveSense.Minimize, 0)

   print(round(so.create_solver(regional, "highs").solve().objective_value, 2))

.. code-block:: text

   99.0

As with variables, ``regional.blocks("region")`` also gives the group,
and ``regional.block("region[east]")`` gives a single child.

Nested blocks
-------------

Blocks can be attached to blocks, to any depth. Lookups go one level at
a time:

.. code-block:: python

   site = so.Block()
   site.x = so.Var(bounds=(0, 1))
   plant = so.Block()
   plant.site = site
   tree = so.Model()
   tree.plant = plant

   x = tree.plant.site.x

A block used as a whole model
-----------------------------

``so.Model.from_block(block, sense)`` turns a standalone block into a
model, keeping the block's objective. This is useful when a block
arrives from elsewhere, for example from another process:

.. code-block:: python

   alone = so.Model.from_block(build_facility(40, 2.0), so.ObjectiveSense.Minimize)

Next, :doc:`piecewise` models functions made of straight segments, such
as a tiered price.
