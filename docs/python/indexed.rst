Indexed variables and constraints
=================================

Real models have families of variables, such as "how much to ship along
each route", rather than a handful of named ones. ``add_vars`` and
``add_constraints`` declare a whole family in one call, one item per key
of an index.

This page builds a transportation problem: ship goods from two
warehouses to three customers at minimum cost, without exceeding any
warehouse's supply and while meeting every customer's demand.

Step 1: the data
----------------

.. code-block:: python

   import itertools
   import saiopt as so

   warehouses = ["W1", "W2"]
   customers = ["C1", "C2", "C3"]
   supply = {"W1": 20, "W2": 30}
   demand = {"C1": 15, "C2": 20, "C3": 15}
   cost = {
       ("W1", "C1"): 4, ("W1", "C2"): 6, ("W1", "C3"): 9,
       ("W2", "C1"): 5, ("W2", "C2"): 3, ("W2", "C3"): 2,
   }

Step 2: one variable per route
------------------------------

The index can be any iterable of keys. For a two-dimensional index, use
tuples; ``itertools.product`` builds every combination:

.. code-block:: python

   model = so.Model()
   routes = list(itertools.product(warehouses, customers))
   ship = model.add_vars("ship", routes, bounds=(0, so.inf))

``add_vars`` returns a dict from key to variable, in the order of the
index. Index it with the key, and with a tuple key you can leave out the
parentheses:

.. code-block:: python

   first = ship["W1", "C1"]

Each variable is named ``name[key]``, here ``ship[(W1, C1)]``, so it can
also be found by name later (see `Looking groups back up`_ below).

Keys can be any Python values that compare with each other: strings,
numbers, tuples of them, and so on.

Per-key bounds and kinds
~~~~~~~~~~~~~~~~~~~~~~~~

``kind`` and ``bounds`` apply to every variable in the group. Either one
can instead be a function that takes the key and returns that variable's
value. For example, to cap each route at the warehouse's supply:

.. code-block:: python

   capped = model.add_vars("capped", routes, bounds=lambda route: (0, supply[route[0]]))

Step 3: one constraint per warehouse and per customer
-----------------------------------------------------

``add_constraints(name, keys, rule)`` calls ``rule(key)`` for every key
and adds the constraint it returns, named ``name[key]``:

.. code-block:: python

   supply_limit = model.add_constraints(
       "supply_limit", warehouses,
       lambda w: so.quicksum(ship[w, c] for c in customers) <= supply[w],
   )
   demand_min = model.add_constraints(
       "demand_min", customers,
       lambda c: so.quicksum(ship[w, c] for w in warehouses) >= demand[c],
   )

Like ``add_vars``, it returns a dict from key to constraint.

Step 4: objective and solve
---------------------------

.. code-block:: python

   model.set_objective(
       so.ObjectiveSense.Minimize,
       so.quicksum(cost[r] * ship[r] for r in routes),
   )

   result = so.create_solver(model, "highs").solve()
   print("minimum cost:", result.objective_value)

.. code-block:: text

   minimum cost: 165.0

``result.values(group)`` reads a whole group at once. Pass it the dict
``add_vars`` returned and you get back a dict with the same keys:

.. code-block:: python

   shipped = result.values(ship)
   for (w, c), amount in shipped.items():
       if amount > 1e-6:
           print(f"{w} -> {c}: {amount}")

.. code-block:: text

   W1 -> C1: 15.0
   W1 -> C2: 5.0
   W2 -> C2: 15.0
   W2 -> C3: 15.0

Sparse indexes
--------------

An index doesn't have to be a full product. If each warehouse can reach
only some customers, list the routes that exist and build both
constraint families from that one list:

.. code-block:: python

   reachable = {"W1": ["C1", "C2"], "W2": ["C2", "C3"]}
   arcs = [(w, c) for w, cs in reachable.items() for c in cs]

   sparse = so.Model()
   flow = sparse.add_vars("flow", arcs, bounds=(0, so.inf))
   sparse.add_constraints(
       "demand_min", customers,
       lambda c: so.quicksum(flow[w, c2] for w, c2 in arcs if c2 == c) >= demand[c],
   )

Looking groups back up
----------------------

You don't have to keep the dicts around. A model remembers every group
by the name you declared it under:

.. code-block:: python

   ship_again = model.vars("ship")              # the same dict, key -> Variable
   one = model.var("ship[(W2, C3)]")            # a single variable, by its full name
   limits = model.constraints("supply_limit")   # key -> ConstraintRef
   w1_limit = model.constraint("supply_limit[W1]")

   print(one.is_same(ship["W2", "C3"]))

.. code-block:: text

   True

``var`` and ``constraint`` return ``None`` when nothing has that name.
``vars`` and ``constraints`` raise ``RuntimeError`` for a group name
that was never declared. Declaring a second group under a name that is
already taken also raises ``RuntimeError``.

This matters most once a model is split into blocks, where the code that
created a variable may be long gone by the time you need it. That's the
next page, :doc:`blocks`.
