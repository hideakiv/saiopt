Indexed variables and constraints
=================================

Real models have families of variables, such as "how much to ship along
each route", rather than a handful of named ones. ``so.Vars`` and
``so.Constraints`` declare a whole family at once, one item per key of
an index.

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
   model.ship = so.Vars(routes, bounds=(0, so.inf))

Reading ``model.ship`` gives a dict from key to variable, in the order of
the index. Index it with the key, and with a tuple key you can leave out
the parentheses:

.. code-block:: python

   ship = model.ship
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

   model.capped = so.Vars(routes, bounds=lambda route: (0, supply[route[0]]))

Step 3: one constraint per warehouse and per customer
-----------------------------------------------------

``so.Constraints(keys, rule)`` calls ``rule(key)`` for every key and adds
the constraint it returns, named ``name[key]``:

.. code-block:: python

   model.supply_limit = so.Constraints(
       warehouses, lambda w: so.quicksum(ship[w, c] for c in customers) <= supply[w]
   )
   model.demand_min = so.Constraints(
       customers, lambda c: so.quicksum(ship[w, c] for w in warehouses) >= demand[c]
   )

Reading ``model.supply_limit`` gives a dict from key to constraint.

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
``model.ship`` gives and you get back a dict with the same keys:

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
   sparse.flow = so.Vars(arcs, bounds=(0, so.inf))
   sparse.demand_min = so.Constraints(
       customers, lambda c: so.quicksum(sparse.flow[w, c2] for w, c2 in arcs if c2 == c) >= demand[c]
   )

Looking groups back up
----------------------

Reading an attribute such as ``model.ship`` looks the group up by its
name. The same lookups are also methods, which take the name as a
string. That's how you reach a single item, whose full name isn't an
identifier, or a group whose name you only have as a string:

.. code-block:: python

   ship_again = model.vars("ship")              # the same dict as model.ship
   one = model.var("ship[(W2, C3)]")            # a single variable, by its full name
   limits = model.constraints("supply_limit")   # key -> ConstraintRef
   w1_limit = model.constraint("supply_limit[W1]")

   print(one.is_same(model.ship["W2", "C3"]))

.. code-block:: text

   True

``var`` and ``constraint`` return ``None`` when nothing has that name.
``vars`` and ``constraints`` raise ``RuntimeError`` for a group name
that was never declared. Reading an attribute that names nothing raises
``AttributeError``, as for any Python object.

``vars``, ``constraints``, and ``blocks`` also take the keys you want,
and then look each item up by its full name, ``name[key]``, in the order
given:

.. code-block:: python

   some = model.vars("ship", [("W2", "C3"), ("W1", "C1")])
   print(list(some))

.. code-block:: text

   [('W2', 'C3'), ('W1', 'C1')]

This matters most once a model is split into blocks, where the code that
created a variable may be long gone by the time you need it. That's the
next page, :doc:`blocks`.
