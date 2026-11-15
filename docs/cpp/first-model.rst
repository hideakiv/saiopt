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

Step 1: include the headers and create a model
----------------------------------------------

A ``Model`` is the thing you solve. It starts out empty.

.. code-block:: cpp

   #include "saiopt/macros.hpp"
   #include "saiopt/model.hpp"
   #include "saiopt/solver.hpp"

   using namespace saiopt;

   Model model;

Step 2: add variables
---------------------

``SAIOPT_VAR(model, name, ...)`` adds a variable to ``model`` and
declares a local C++ variable of the same name holding its handle. The
identifier you write is also the variable's name inside saiopt, so you
can look it up later as ``model.var("bread")`` without repeating the
string.

Variables are continuous and unbounded by default. Give these a lower
bound of 0 (you can't buy negative bread); ``kInfinity`` is saiopt's
infinite bound.

.. code-block:: cpp

   SAIOPT_VAR(model, bread, .bounds = {0.0, kInfinity});
   SAIOPT_VAR(model, milk, .bounds = {0.0, kInfinity});

The ``.bounds = ...`` argument is a C++20 designated initializer. See
:doc:`expressions` for the other options.

Step 3: add constraints
-----------------------

Arithmetic on variables builds expressions, and comparing an expression
with ``<=``, ``>=``, or ``==`` builds a constraint. ``SAIOPT_CONSTRAINT``
adds one to the model, named after the identifier:

.. code-block:: cpp

   SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
   SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);

``protein`` and ``calories`` are now ``ConstraintRef`` handles, which
you can use later to read dual values or change bounds.

Step 4: set the objective
-------------------------

The objective is a direction plus an expression:

.. code-block:: cpp

   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);

You can print a model to check what you've built:

.. code-block:: cpp

   model.print();

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

``create_solver`` hands the model to the backend registered under a
name, here HiGHS, and returns a ``std::unique_ptr<Solver>``. Calling
``solve()`` on it returns a ``SolveResult``:

.. code-block:: cpp

   auto solver = create_solver(model, "highs");
   SolveResult result = solver->solve();

Step 6: read the solution
-------------------------

Check the status first: only a solve that found a solution has values to
read.

.. code-block:: cpp

   if (result.optimal()) {
       std::printf("cost: %g\n", result.objective_value);
       std::printf("bread: %g\n", result.value(bread));
       std::printf("milk: %g\n", result.value(milk));
   } else {
       std::printf("no optimal solution: %s\n", std::string(to_string(result.status)).c_str());
   }

.. code-block:: text

   cost: 8
   bread: 1
   milk: 2

This model has more than one optimal solution (buying 8/3 units of milk
and no bread also costs 8), so another backend may report different
amounts for the same cost.

To solve with SCIP instead, change only the backend name:
``create_solver(model, "scip")``. Nothing else changes, and no
backend-specific header is needed.

The whole program
-----------------

.. check: program

.. code-block:: cpp

   #include <cstdio>

   #include "saiopt/macros.hpp"
   #include "saiopt/model.hpp"
   #include "saiopt/solver.hpp"

   int main() {
       using namespace saiopt;

       Model model;
       SAIOPT_VAR(model, bread, .bounds = {0.0, kInfinity});
       SAIOPT_VAR(model, milk, .bounds = {0.0, kInfinity});
       SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
       SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);
       SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);

       auto solver = create_solver(model, "highs");
       SolveResult result = solver->solve();
       std::printf("cost %g: bread %g, milk %g\n", result.objective_value, result.value(bread),
                   result.value(milk));
       return 0;
   }

A longer version with four foods, per-food purchase limits, and a fat
limit is ``examples/diet_problem.cpp``.

Next, :doc:`expressions` covers what you can write inside constraints and
objectives.
