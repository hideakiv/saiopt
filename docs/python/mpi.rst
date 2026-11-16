Building blocks in parallel with MPI
====================================

Because each :doc:`block <blocks>` is built independently, separate
processes can build different parts of one model at the same time.
``saiopt.mpi`` sends blocks between MPI ranks: every rank builds its own
blocks, then sends them to one rank that attaches them to a model and
solves it. On large models, this cuts the time spent building the model
roughly in proportion to the number of ranks.

Setup
-----

``saiopt.mpi`` needs `mpi4py <https://mpi4py.readthedocs.io/>`_, built
against the MPI implementation you run your jobs with:

.. code-block:: console

   $ pip install .[mpi]

The module has three functions:

``gather_blocks(block, root=0, tag=0, comm=None)``
   Collects every rank's ``block`` on rank ``root``. Every rank in the
   communicator must call it. On ``root`` it returns a list of blocks in
   rank order, with ``root``'s own block as its own entry. Every other
   rank gets ``None``.

``send_block(block, dest, tag=0, comm=None)``
   Sends a copy of ``block`` to rank ``dest`` and waits until the send
   completes. ``block`` itself is unchanged.

``recv_block(source=MPI.ANY_SOURCE, tag=MPI.ANY_TAG, comm=None)``
   Waits for a block from ``source`` and returns it as a new standalone
   ``Block``, ready to attach to a parent.

``comm`` defaults to ``MPI.COMM_WORLD``.

A worked example
----------------

Each rank builds one region with its own capacity and unit cost. Rank 0
gathers every region, ties them together with a demand constraint, and
solves.

Step 1: every rank builds its block
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. check: skip

.. code-block:: python

   from mpi4py import MPI

   import saiopt as so
   import saiopt.mpi

   comm = MPI.COMM_WORLD
   rank, size = comm.Get_rank(), comm.Get_size()

   capacity = 10.0 + rank
   cost_per_unit = 5.0 - 0.5 * rank

   region = so.Block()
   region.produce = so.Var(bounds=(0, capacity))
   region.set_objective(cost_per_unit * region.produce)

Nothing here needs to coordinate with other ranks.

Step 2: gather the blocks on rank 0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. check: skip

.. code-block:: python

   regions = saiopt.mpi.gather_blocks(region)   # a list on rank 0, None elsewhere

Step 3: attach and solve on rank 0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A received block is a new object, so variables from another rank's
block don't apply to it. Read them through the attached block instead,
which looks them up by name:

.. check: skip

.. code-block:: python

   if rank == 0:
       model = so.Model()
       model.region = so.Blocks(range(size), lambda i: regions[i])
       produced = {i: block.produce for i, block in model.region.items()}
       model.meet_demand = so.quicksum(produced.values()) >= 25.0
       model.set_objective(so.ObjectiveSense.Minimize, 0)

       result = so.create_solver(model, "highs").solve()
       print(f"built from {size} ranks, minimum cost = {result.objective_value:.2f}")
       for i, var in produced.items():
           print(f"  region{i} produced {result[var]:.2f}")

Step 4: run it
~~~~~~~~~~~~~~

The full program is ``python/examples/mpi_parallel_build.py`` in the
repository:

.. check: run

.. code-block:: console

   $ mpiexec -n 4 python python/examples/mpi_parallel_build.py
   built from 4 ranks, minimum cost = 93.50
     region0 produced 0.00
     region1 produced 0.00
     region2 produced 12.00
     region3 produced 13.00

Mixing Python and C++ ranks
---------------------------

A block travels as the bytes of ``Block.serialize()``, in a plain
``MPI_BYTE`` message. That's the same format the C++
``saiopt::mpi::send_block`` and ``recv_block`` use, so Python and C++
ranks can exchange blocks within one MPI job.

You can also use ``block.serialize()`` and ``so.Block.deserialize(data)``
directly, for example to save a block to disk or send it some other way.

A received block keeps every name, including its groups and their keys,
so ``region.produce`` or ``chunk.region[3]`` work on it as they did on
the sender. Keys come through when they're ints, floats, strings, or
tuples of those. A group with any other key, such as a ``bool`` or a
custom object, arrives without its keys: reading it raises
``RuntimeError``, but ``block.vars(name, keys)`` and lookups by full
name still find its items.

Starting subprocesses
---------------------

Importing ``saiopt.mpi`` initializes MPI through mpi4py. Some MPI builds,
such as MPICH over libfabric, then abort the process on any ``fork()``,
including the one ``subprocess`` uses. Start any subprocesses before
importing ``saiopt.mpi``.
