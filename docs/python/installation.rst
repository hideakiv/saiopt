Installation
============

saiopt needs Python 3.9 or newer, a C++20 compiler, and CMake 3.21 or
newer. From a checkout of the repository, run:

.. code-block:: console

   $ pip install .

This builds the C++ library, both solver backends, and HiGHS and SCIP
themselves, all linked into one extension module, so nothing else needs
to be installed. The first build compiles HiGHS and SCIP and takes a
while. Later builds reuse the ``build-python/`` directory and only
rebuild what changed.

Check that it works:

.. code-block:: console

   $ python -c "import saiopt; print(saiopt.create_solver)"

Optional extras
---------------

``pip install .[test]``
   Adds pytest. Run the test suite with ``pytest`` from the repository
   root.

``pip install .[mpi]``
   Adds mpi4py, which :doc:`mpi` needs. mpi4py has to be built against
   the same MPI implementation you run your jobs with.

Conventions in this guide
-------------------------

Every example imports saiopt as ``so``:

.. code-block:: python

   import saiopt as so
