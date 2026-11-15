Indexed variables and constraints
=================================

Real models have families of variables, such as "how much to ship along
each route", rather than a handful of named ones. ``SAIOPT_VARS`` and
``SAIOPT_CONSTRAINTS`` declare a whole family in one statement, one item
per key of an index.

This page builds a transportation problem: ship goods from two
warehouses to three customers at minimum cost, without exceeding any
warehouse's supply and while meeting every customer's demand. The
repository has a similar complete program in
``examples/transportation.cpp``.

Step 1: the data
----------------

.. code-block:: cpp

   #include "saiopt/cross.hpp"

   std::vector<std::string> warehouses{"W1", "W2"};
   std::vector<std::string> customers{"C1", "C2", "C3"};
   std::map<std::string, double> supply{{"W1", 20.0}, {"W2", 30.0}};
   std::map<std::string, double> demand{{"C1", 15.0}, {"C2", 20.0}, {"C3", 15.0}};
   std::map<std::pair<std::string, std::string>, double> cost{
       {{"W1", "C1"}, 4.0}, {{"W1", "C2"}, 6.0}, {{"W1", "C3"}, 9.0},
       {{"W2", "C1"}, 5.0}, {{"W2", "C2"}, 3.0}, {{"W2", "C3"}, 2.0},
   };

Step 2: one variable per route
------------------------------

``SAIOPT_VARS(block, name, index, ...)`` adds one variable per key in
``index``, which can be any range: a ``std::vector``, a ``std::set``, and
so on. For a multi-dimensional index, ``cross(a, b, ...)`` from
``saiopt/cross.hpp`` builds every combination as a
``std::vector<std::tuple<...>>``:

.. code-block:: cpp

   Model model;
   auto routes = cross(warehouses, customers);   // vector<tuple<string, string>>
   SAIOPT_VARS(model, ship, routes, .bounds = {0.0, kInfinity});

``ship`` is an ``Indexed<std::tuple<std::string, std::string>, Variable>``.
Look an item up with ``operator[]``. With a tuple key you can write the
key in braces:

.. code-block:: cpp

   Variable first = ship[{"W1", "C1"}];

``operator[]`` throws for a key that isn't in the group. ``get(key)``
returns a pointer instead, which is ``nullptr`` for a missing key.
Iterating an ``Indexed`` gives ``(key, value)`` pairs, ordered by key.

Each variable is named ``name[key]``, here ``ship[(W1, C1)]``, so it can
also be found by name later (see `Looking groups back up`_ below).

Per-key bounds and kinds
~~~~~~~~~~~~~~~~~~~~~~~~

``.kind`` and ``.bounds`` work as in ``SAIOPT_VAR`` and apply to the
whole group. Either can instead be a function of the key, which returns
that variable's value. A bounds function returns a ``std::tuple`` of
``(lower, upper)``. For example, to cap each route at its warehouse's
supply:

.. code-block:: cpp

   SAIOPT_VARS(model, capped, routes, .bounds = [&](const auto& route) {
       return std::tuple{0.0, supply.at(std::get<0>(route))};
   });

Step 3: one constraint per warehouse and per customer
-----------------------------------------------------

``SAIOPT_CONSTRAINTS(block, name, index, rule)`` calls ``rule(key)`` for
every key and adds the constraint it returns, named ``name[key]``. Wrap
the lambda in parentheses: a lambda's commas would otherwise split it
into several macro arguments.

.. code-block:: cpp

   SAIOPT_CONSTRAINTS(model, supply_limit, warehouses, ([&](const std::string& w) {
       LinearExpression shipped;
       for (const auto& c : customers) shipped += ship[{w, c}];
       return shipped <= supply.at(w);
   }));

   SAIOPT_CONSTRAINTS(model, demand_min, customers, ([&](const std::string& c) {
       LinearExpression received;
       for (const auto& w : warehouses) received += ship[{w, c}];
       return received >= demand.at(c);
   }));

``supply_limit`` and ``demand_min`` are ``Indexed<std::string,
ConstraintRef>`` groups.

Step 4: objective and solve
---------------------------

.. code-block:: cpp

   LinearExpression total_cost;
   for (const auto& [w, c] : routes) total_cost += cost.at({w, c}) * ship[{w, c}];
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, total_cost);

   SolveResult result = create_solver(model, "highs")->solve();
   std::printf("minimum cost: %g\n", result.objective_value);
   for (const auto& [w, c] : routes) {
       double amount = result.value(ship[{w, c}]);
       if (amount > 1e-6) std::printf("%s -> %s: %g\n", w.c_str(), c.c_str(), amount);
   }

.. code-block:: text

   minimum cost: 165
   W1 -> C1: 15
   W1 -> C2: 5
   W2 -> C2: 15
   W2 -> C3: 15

Sparse indexes
--------------

An index doesn't have to be a full product. If each warehouse can reach
only some customers, list the arcs that exist and use that list as the
index:

.. code-block:: cpp

   std::map<std::string, std::vector<std::string>> reachable{
       {"W1", {"C1", "C2"}},
       {"W2", {"C2", "C3"}},
   };
   std::vector<std::tuple<std::string, std::string>> arcs;
   for (const auto& [w, cs] : reachable) {
       for (const auto& c : cs) arcs.emplace_back(w, c);
   }

   Model sparse;
   SAIOPT_VARS(sparse, flow, arcs, .bounds = {0.0, kInfinity});

Build every constraint family from this one list, so the data can't
drift out of sync. ``examples/jagged_index.cpp`` is a complete program
that does this.

Looking groups back up
----------------------

You don't have to keep the group handles around. A model remembers every
group by the name it was declared under:

.. code-block:: cpp

   // The whole group, with key access. The key type must be given.
   auto ship_again = model.vars<std::tuple<std::string, std::string>>("ship");

   // The same variables as a std::vector, in declaration order.
   std::vector<Variable> ship_list = model.vars("ship");

   // A single item, by its full name. Returns std::optional<Variable>.
   std::optional<Variable> one = model.var("ship[(W2, C3)]");

   std::printf("%d %zu\n", one->is_same(ship[{"W2", "C3"}]), ship_list.size());

.. code-block:: text

   1 6

``constraints`` and ``constraint`` work the same way for constraint
groups.

``var`` and ``constraint`` return an empty ``std::optional`` when nothing
has that name. ``vars`` and ``constraints`` throw ``std::runtime_error``
for a group name that was never declared, and the typed form
``vars<K>`` also throws if ``K`` isn't the key type the group was
declared with. Declaring a second group under a name that is already
taken throws too.

This matters most once a model is split into blocks, where the code that
created a variable may be long gone by the time you need it.
``examples/variable_lookup.cpp`` shows every form of lookup. That's the
next page, :doc:`blocks`.
