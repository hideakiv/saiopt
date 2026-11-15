Building blocks in parallel with MPI
====================================

Because each :doc:`block <blocks>` is built independently, separate
processes can build different parts of one model at the same time.
``saiopt_mpi`` sends blocks between MPI ranks: every rank builds its own
blocks, then sends them to one rank that attaches them to a model and
solves it. On large models, this cuts the time spent building the model
roughly in proportion to the number of ranks.

Setup
-----

``saiopt_mpi`` is built when ``SAIOPT_WITH_MPI`` is on (the default) and
needs an MPI implementation. Link your program against it, plus a
backend:

.. code-block:: cmake

   target_link_libraries(my_app PRIVATE
     saiopt_mpi
     -Wl,--no-as-needed saiopt_highs -Wl,--as-needed)

``saiopt/mpi.hpp`` declares two functions in namespace ``saiopt::mpi``:

``void send_block(const Block& block, int dest, int tag, MPI_Comm comm = MPI_COMM_WORLD)``
   Sends a copy of ``block`` to rank ``dest`` and waits until the send
   completes. ``block`` itself is unchanged.

``Block recv_block(int source, int tag, MPI_Comm comm = MPI_COMM_WORLD)``
   Waits for a block from rank ``source`` and returns it as a new
   standalone ``Block``, ready to attach.

You call ``MPI_Init`` and ``MPI_Finalize`` yourself.

A worked example
----------------

Each rank builds one region with its own capacity and unit cost. Rank 0
receives every region, ties them together with a demand constraint, and
solves. The complete program is ``examples/mpi_parallel_build.cpp``.

Step 1: every rank builds its block
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. cpp-scope: skip

.. code-block:: cpp

   #include <mpi.h>
   #include "saiopt/mpi.hpp"

   MPI_Init(&argc, &argv);
   int rank = 0, size = 1;
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
   MPI_Comm_size(MPI_COMM_WORLD, &size);

   double capacity = 10.0 + rank;
   double cost_per_unit = 5.0 - 0.5 * rank;

   Block region;
   SAIOPT_VAR(region, produce, .bounds = {0.0, capacity});
   SAIOPT_OBJECTIVE(region, cost_per_unit * produce);

Nothing here needs to coordinate with other ranks.

Step 2: send every block to rank 0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. cpp-scope: skip

.. code-block:: cpp

   const int tag = 0;
   if (rank != 0) {
       mpi::send_block(region, /*dest=*/0, tag);
   } else {
       std::vector<Block> regions;
       regions.push_back(std::move(region));
       for (int source = 1; source < size; ++source) {
           regions.push_back(mpi::recv_block(source, tag));
       }
       // ... step 3
   }

Step 3: attach and solve on rank 0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A received block is a new object, with new internal ids, so ``Variable``
handles from another rank don't apply to it. Look its variables up by
name instead:

.. cpp-scope: skip

.. code-block:: cpp

   std::vector<std::size_t> ids(regions.size());
   std::iota(ids.begin(), ids.end(), 0);

   Model model;
   SAIOPT_BLOCKS(model, attached, ids, ([&](std::size_t i) { return std::move(regions[i]); }));

   LinearExpression total;
   std::vector<Variable> produce_of;
   for (std::size_t i : ids) {
       produce_of.push_back(*model.block(attached[i]).var("produce"));
       total += produce_of.back();
   }
   SAIOPT_CONSTRAINT(model, meet_demand, total >= 25.0);
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, LinearExpression{});

   SolveResult result = create_solver(model, "highs")->solve();

Step 4: run it
~~~~~~~~~~~~~~

.. code-block:: console

   $ mpirun -n 4 ./build/examples/mpi_parallel_build
   built from 4 ranks, minimum cost = 93.50
     region0 produced 0.00
     region1 produced 0.00
     region2 produced 12.00
     region3 produced 13.00

Serialization without MPI
-------------------------

The MPI functions are thin wrappers over ``Block::serialize()``, which
turns a block and everything attached below it into a flat byte buffer,
and ``Block::deserialize()``, which rebuilds it:

.. code-block:: cpp

   Block original;
   SAIOPT_VAR(original, z, .bounds = {0.0, 3.0});
   SAIOPT_OBJECTIVE(original, -1.0 * z);

   std::vector<std::byte> bytes = original.serialize();
   Model rebuilt(Block::deserialize(bytes), ObjectiveSense::Minimize);
   std::printf("%g\n", create_solver(rebuilt, "highs")->solve().objective_value);

.. code-block:: text

   -3

Use these to save a block to disk, or to move one between processes some
other way. The bytes are the same ones the Python ``saiopt.mpi`` module
sends, so Python and C++ ranks can exchange blocks within one MPI job.
