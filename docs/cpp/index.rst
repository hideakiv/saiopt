saiopt for C++
==============

saiopt (Structured Algebraic Interface for Optimization) is a C++20
modeling library for linear and mixed-integer programs, with optional
quadratic objectives. You write a model as algebra --
``4.0 * bread + 8.0 * milk >= 20.0`` -- and solve it with
`HiGHS <https://highs.dev/>`_ or `SCIP <https://scipopt.org/>`_.

A saiopt model is a tree of **blocks**. Each block has its own variables,
constraints, and objective contribution, and you can build one
independently of the others: in a separate function, or in a separate
MPI process. You then attach them to one model and solve them together.

The core library doesn't depend on any solver. Backends implement an
abstract ``Solver`` interface and register themselves under a name, so
switching from HiGHS to SCIP means changing one string, and you can add
your own backend without touching saiopt.

This guide walks through saiopt step by step, from a first model to
writing a backend. Each page builds on the previous ones. The complete
programs in the repository's ``examples/`` directory are linked from the
pages that use them.

saiopt also has a Python package with the same API; see the
`Python guide <../python/>`_.

.. toctree::
   :maxdepth: 2
   :caption: Guide

   installation
   first-model
   expressions
   indexed
   blocks
   solving
   modifying
   mpi
   custom-backend
