Installation
============

Requirements
------------

* A C++20 compiler
* CMake 3.21 or newer
* Optional: an MPI implementation, for ``saiopt_mpi``

You don't need to install HiGHS or SCIP. CMake downloads and builds them
as part of the configure step.

Building saiopt
---------------

From a checkout of the repository:

.. code-block:: console

   $ cmake -S . -B build
   $ cmake --build build -j

This produces these libraries:

``saiopt``
   The core modeling library. It doesn't depend on any solver.

``saiopt_highs``
   The HiGHS backend, registered under the name ``"highs"``.

``saiopt_scip``
   The SCIP backend (with SoPlex as its LP solver), registered under the
   name ``"scip"``.

``saiopt_mpi``
   Sends blocks between MPI processes (see :doc:`mpi`). Configuring
   fails if CMake can't find MPI. If you don't have MPI, turn this off
   with ``-DSAIOPT_WITH_MPI=OFF``.

Run the tests and an example to check the build:

.. code-block:: console

   $ ./build/tests/saiopt_tests
   $ ./build/examples/diet_problem

CMake options
~~~~~~~~~~~~~

===========================  =======  ==============================================
Option                       Default  Effect
===========================  =======  ==============================================
``SAIOPT_WITH_HIGHS``        ``ON``   Build ``saiopt_highs`` (and HiGHS)
``SAIOPT_WITH_SCIP``         ``ON``   Build ``saiopt_scip`` (and SCIP and SoPlex)
``SAIOPT_WITH_MPI``          ``ON``   Build ``saiopt_mpi``; requires MPI
``SAIOPT_BUILD_EXAMPLES``    ``ON``   Build the programs in ``examples/``
``SAIOPT_BUILD_TESTS``       ``ON``   Build the test suite (fetches Catch2)
===========================  =======  ==============================================

For example, to build with HiGHS only and no MPI:

.. code-block:: console

   $ cmake -S . -B build -DSAIOPT_WITH_SCIP=OFF -DSAIOPT_WITH_MPI=OFF

Using saiopt in your own project
--------------------------------

saiopt is meant to be built as part of your project, with
``FetchContent`` (or ``add_subdirectory`` on a local checkout):

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.21)
   project(my_app LANGUAGES CXX)
   set(CMAKE_CXX_STANDARD 20)

   include(FetchContent)
   set(SAIOPT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
   set(SAIOPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
   FetchContent_Declare(
     saiopt
     GIT_REPOSITORY https://github.com/hideakiv/saiopt.git
     GIT_TAG main
   )
   FetchContent_MakeAvailable(saiopt)

   add_executable(my_app main.cpp)
   target_link_libraries(my_app PRIVATE
     saiopt
     -Wl,--no-as-needed saiopt_highs saiopt_scip -Wl,--as-needed)

Linking a backend
~~~~~~~~~~~~~~~~~

Each backend registers itself with ``create_solver`` when its shared
library is loaded. Your code only refers to a backend by its name
(``"highs"``), never by any of its symbols. Many Linux toolchains link
with ``--as-needed`` by default, which drops a shared library that no
symbol is used from. The backend then never loads and
``create_solver(model, "highs")`` throws "no solver registered". The
``-Wl,--no-as-needed ... -Wl,--as-needed`` pair around the backends
keeps them linked.

Headers
~~~~~~~

All headers live under ``saiopt/``. Most programs need these three:

.. code-block:: cpp

   #include "saiopt/macros.hpp"   // SAIOPT_VAR, SAIOPT_CONSTRAINT, ...
   #include "saiopt/model.hpp"    // Model, Block, expressions, SolveResult
   #include "saiopt/solver.hpp"   // create_solver, Solver, SolverOptions

Everything is in namespace ``saiopt``. The examples in this guide assume
``using namespace saiopt;``.
