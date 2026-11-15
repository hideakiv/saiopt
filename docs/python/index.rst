saiopt for Python
=================

saiopt (Structured Algebraic Interface for Optimization) is a modeling
library for linear and mixed-integer programs, with optional quadratic
objectives. You write a model as algebra -- ``4 * bread + 8 * milk >= 20``
-- and solve it with `HiGHS <https://highs.dev/>`_ or
`SCIP <https://scipopt.org/>`_.

A saiopt model is a tree of **blocks**. Each block has its own variables,
constraints, and objective contribution, and you can build one
independently of the others: in a separate function, or in a separate
MPI process. You then attach them to one model and solve them together.

This guide walks through saiopt step by step, from a first model to
building one in parallel with MPI. Each page builds on the previous ones.
saiopt is also a C++ library, and the Python API mirrors the C++ one
closely; the `C++ guide <../cpp/>`_ covers that side.

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
