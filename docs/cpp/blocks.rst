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
block. ``examples/hierarchical_facilities.cpp`` is a complete program
along the same lines.

Step 1: a function that builds one block
----------------------------------------

Every macro that works on a ``Model`` works on a ``Block`` too. A
block's objective has no direction of its own, so ``SAIOPT_OBJECTIVE``
takes only the expression, and the model's direction applies to it.

.. cpp-scope: file

.. code-block:: cpp

   Block build_facility(double period_capacity, double unit_cost) {
       std::vector<int> periods{0, 1, 2};

       Block block;
       SAIOPT_VARS(block, produce, periods, .bounds = {0.0, period_capacity});
       SAIOPT_VAR(block, total, .bounds = {0.0, kInfinity});

       LinearExpression sum;
       for (const auto& [period, var] : produce) sum += var;
       SAIOPT_CONSTRAINT(block, total_link, total == sum);

       SAIOPT_OBJECTIVE(block, unit_cost * total);
       return block;
   }

``Block`` is move-only, so return it by value and attach it with
``std::move``.

Step 2: attach blocks to a model
--------------------------------

``SAIOPT_BLOCK(parent, name, child)`` moves ``child`` into ``parent``
under ``name``. Its objective is added to the parent's as is.
``SAIOPT_BLOCK_SCALED(parent, name, child, multiplier)`` scales it by
``multiplier`` first. Both declare a ``BlockRef`` handle named after the
identifier:

.. code-block:: cpp

   Model model;
   SAIOPT_BLOCK(model, north, build_facility(40.0, 2.0));
   SAIOPT_BLOCK_SCALED(model, south, build_facility(30.0, 1.5), 1.2);

Step 3: reach into a child
--------------------------

A child's names live in the child. ``model.block(ref)`` gives a
``Block&`` for a ``BlockRef``, and ``model.block("name")`` gives a
``Block*``, which is ``nullptr`` if there is no such child. Then look the
variable up in the child:

.. code-block:: cpp

   Variable north_total = *model.block(north).var("total");
   Variable south_total = *model.block("south")->var("total");

``var`` returns ``std::optional<Variable>``, empty if the name doesn't
exist. Handles you got from a block before attaching it stay valid after
the move, so if ``build_facility`` had also returned ``total``, you could
use that directly.

Constraints can only point *down* the tree. A block's constraints and
objective may use its own variables and those of the blocks attached
below it, but never a parent's. The root can therefore tie the
facilities together:

.. code-block:: cpp

   std::vector<std::string> sites{"north", "south"};
   SAIOPT_VARS(model, open, sites, .kind = VarKind::Binary);
   SAIOPT_CONSTRAINT(model, north_cap, north_total <= 100.0 * open["north"]);
   SAIOPT_CONSTRAINT(model, south_cap, south_total <= 80.0 * open["south"]);
   SAIOPT_CONSTRAINT(model, demand, north_total + south_total >= 90.0);

A constraint inside ``north`` that used ``open`` would make
``create_solver`` throw ``std::runtime_error``.

Step 4: solve the whole tree
----------------------------

The root's objective is added to every block's contribution, each scaled
by its multiplier:

.. code-block:: cpp

   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 50.0 * open["north"] + 30.0 * open["south"]);

   SolveResult result = create_solver(model, "highs")->solve();
   std::printf("total cost: %g\n", result.objective_value);
   std::printf("north: %g south: %g\n", result.value(north_total), result.value(south_total));

.. code-block:: text

   total cost: 230
   north: 90 south: 0

The total adds the root's fixed costs (50 or 30 per open facility) to
each facility's production cost: ``2.0 * north_total`` for north and
``1.2 * 1.5 * south_total`` for south, scaled by its multiplier. South is
cheaper per unit, but its capacity of 80 can't cover the demand of 90 on
its own, so opening only north is cheapest.

Many blocks at once
-------------------

``SAIOPT_BLOCKS(parent, name, index, rule)`` attaches one block per key:
``rule(key)`` builds it, and it's named ``name[key]``.
``SAIOPT_BLOCKS_SCALED`` takes a fifth argument, a function of the key
that returns the multiplier. Both declare an ``Indexed<K, BlockRef>``.
As with ``SAIOPT_CONSTRAINTS``, wrap each lambda in parentheses:

.. code-block:: cpp

   std::vector<std::string> region_names{"east", "west"};
   std::map<std::string, double> capacity{{"east", 25.0}, {"west", 35.0}};
   std::map<std::string, double> unit_cost{{"east", 1.0}, {"west", 1.1}};

   Model regional;
   SAIOPT_BLOCKS_SCALED(
       regional, region, region_names,
       ([&](const std::string& r) { return build_facility(capacity.at(r), unit_cost.at(r)); }),
       ([&](const std::string& r) { return r == "east" ? 1.0 : 0.9; }));

Build constraints that span the blocks *after* they're all attached.
Look up what you need first, then use it:

.. code-block:: cpp

   LinearExpression all_production;
   for (const auto& name : region_names) {
       all_production += *regional.block(region[name]).var("total");
   }
   SAIOPT_CONSTRAINT(regional, demand_all, all_production >= 100.0);
   SAIOPT_OBJECTIVE(regional, ObjectiveSense::Minimize, LinearExpression{});

   std::printf("%g\n", create_solver(regional, "highs")->solve().objective_value);

.. code-block:: text

   99

Don't do those lookups inside a ``SAIOPT_CONSTRAINTS`` rule that
captures the same model. Collect the handles beforehand, as above.

``regional.blocks("region", region_names)`` gives the group back later
as an ``Indexed<std::string, Block*>``, and
``regional.block("region[east]")`` gives a single child.

Nested blocks
-------------

Blocks can be attached to blocks, to any depth. Lookups go one level at
a time:

.. code-block:: cpp

   Block site;
   SAIOPT_VAR(site, x, .bounds = {0.0, 1.0});
   Block plant;
   SAIOPT_BLOCK(plant, site_block, std::move(site));
   Model tree;
   SAIOPT_BLOCK(tree, plant_block, std::move(plant));

   std::optional<Variable> found = tree.block("plant_block")->block("site_block")->var("x");

Give the attached block a different identifier from the local
``Block`` you move in. ``SAIOPT_BLOCK(plant, site, std::move(site))``
would declare a second variable named ``site``, a ``BlockRef``.

A block used as a whole model
-----------------------------

``Model(std::move(block), sense)`` turns a standalone block into a
model, keeping the block's objective. This is useful when a block
arrives from elsewhere, for example from another process:

.. code-block:: cpp

   Model alone(build_facility(40.0, 2.0), ObjectiveSense::Minimize);

Next, :doc:`solving` covers solver options and everything a
``SolveResult`` holds.
