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

Create a ``Block()`` and use the same ``add_var``, ``add_vars``, and
``add_constraint`` calls as on a model. A block's objective has no
direction of its own: ``set_objective`` takes just the expression, and
the model's direction applies to it.

.. code-block:: python

   import saiopt as so

   periods = range(3)

   def build_facility(period_capacity, unit_cost):
       block = so.Block()
       produce = block.add_vars("produce", periods, bounds=(0, period_capacity))
       total = block.add_var("total", bounds=(0, so.inf))
       block.add_constraint("total_link", total == so.quicksum(produce.values()))
       block.set_objective(unit_cost * total)
       return block

Step 2: attach blocks to a model
--------------------------------

``add_block(name, block, multiplier=1.0)`` moves the block into the
parent under ``name``. The block's objective is added to the parent's,
scaled by ``multiplier``. It returns a handle to the attached child:

.. code-block:: python

   model = so.Model()
   north = model.add_block("north", build_facility(period_capacity=40, unit_cost=2.0))
   south = model.add_block("south", build_facility(period_capacity=30, unit_cost=1.5), multiplier=1.2)

After ``add_block``, the original ``Block`` object can't be used any
more: any call on it raises ``RuntimeError``. Use the handle
``add_block`` returned, or look the child up with ``model.block(name)``.

Step 3: reach into a child
--------------------------

A child's names live in the child, so look its variables up through its
handle:

.. code-block:: python

   north_total = north.var("total")
   south_total = model.block("south").var("total")

Constraints can only point *down* the tree. A block's constraints and
objective may use its own variables and those of the blocks attached
below it, but never a parent's. The root can therefore tie the
facilities together:

.. code-block:: python

   is_open = model.add_vars("open", ["north", "south"], kind=so.VarKind.Binary)
   model.add_constraint("north_cap", north_total <= 100 * is_open["north"])
   model.add_constraint("south_cap", south_total <= 80 * is_open["south"])
   model.add_constraint("demand", north_total + south_total >= 90)

A constraint inside ``north`` that used ``is_open`` would be rejected
with a ``RuntimeError`` when you create the solver.

Step 4: solve the whole tree
----------------------------

The root's objective is added to every block's contribution, each
scaled by its multiplier:

.. code-block:: python

   model.set_objective(so.ObjectiveSense.Minimize, 50 * is_open["north"] + 30 * is_open["south"])

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

``add_blocks(name, keys, rule, multiplier=1.0)`` attaches one block per
key: ``rule(key)`` builds it, and it's named ``name[key]``. The
multiplier can also be a function of the key. It returns a dict from key
to child handle:

.. code-block:: python

   capacity = {"east": 25, "west": 35}
   cost = {"east": 1.0, "west": 1.1}

   regional = so.Model()
   regions = regional.add_blocks(
       "region", capacity,
       lambda r: build_facility(capacity[r], cost[r]),
       multiplier=lambda r: 1.0 if r == "east" else 0.9,
   )
   totals = {r: region.var("total") for r, region in regions.items()}
   regional.add_constraint("demand", so.quicksum(totals.values()) >= 100)
   regional.set_objective(so.ObjectiveSense.Minimize, 0)

   print(round(so.create_solver(regional, "highs").solve().objective_value, 2))

.. code-block:: text

   99.0

As with variables, ``regional.blocks("region")`` gives the group back
later, and ``regional.block("region[east]")`` gives a single child.

Nested blocks
-------------

Blocks can be attached to blocks, to any depth. Lookups go one level at
a time:

.. code-block:: python

   site = so.Block()
   site.add_var("x", bounds=(0, 1))
   plant = so.Block()
   plant.add_block("site", site)
   tree = so.Model()
   tree.add_block("plant", plant)

   x = tree.block("plant").block("site").var("x")

A block used as a whole model
-----------------------------

``so.Model.from_block(block, sense)`` turns a standalone block into a
model, keeping the block's objective. This is useful when a block
arrives from elsewhere, for example from another process:

.. code-block:: python

   alone = so.Model.from_block(build_facility(40, 2.0), so.ObjectiveSense.Minimize)

Next, :doc:`piecewise` models functions made of straight segments, such
as a tiered price.
