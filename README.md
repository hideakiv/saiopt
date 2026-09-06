# saiopt
Structured Algebraic Interface for Optimization

A C++20 algebraic modeling library for LP/MILP. The core library is
solver-agnostic: it only depends on the abstract `Solver` interface and a
name-based factory (`saiopt/solver.hpp`), and any backend can implement
it and register itself under a name -- no HiGHS-specific (or any other
backend's) header required to use one. `saiopt_highs` ships one such
backend, registered as `"highs"`, built on [HiGHS](https://highs.dev/)
(linked via its C API).

## Building

```sh
cmake -S . -B build
cmake --build build -j
```

This fetches and builds Catch2, and HiGHS for the `saiopt_highs` backend
(unless configured with `-DSAIOPT_WITH_HIGHS=OFF`), as part of the
configure step. It also looks for an MPI implementation and builds
`saiopt_mpi` (see [MPI](#mpi) below) unless configured with
`-DSAIOPT_WITH_MPI=OFF`.

Run the tests:

```sh
./build/tests/saiopt_tests
```

Run an example:

```sh
./build/examples/diet_problem
```

Other examples: `blocks_basics`, `indexed_macros`, `hierarchical_facilities`,
`transportation`, and `solver_mutation` -- see `examples/` for what each
demonstrates.

## MPI

`Block::serialize()`/`Block::deserialize()` turn a `Block` (with its whole
subtree of variables, constraints, objective, and attached children) into a
flat byte buffer and back, rebuilding via the ordinary public API on the
receiving side so it gets fresh, locally-unique `BlockId`s regardless of
what the sending process's were. `saiopt_mpi` (`saiopt/mpi.hpp`) wraps that
in `saiopt::mpi::send_block`/`recv_block`, for constructing a Model's Block
tree across MPI ranks in parallel -- each rank builds its own Block(s)
independently, then sends them to a coordinating rank to attach and solve.
See `examples/mpi_parallel_build.cpp`:

```sh
mpirun -n 4 ./build/examples/mpi_parallel_build
```

## Example

`SAIOPT_VAR`/`SAIOPT_CONSTRAINT` (and their indexed forms `SAIOPT_VARS`/
`SAIOPT_CONSTRAINTS`, plus `SAIOPT_BLOCK[_SCALED]`/`SAIOPT_BLOCKS[_SCALED]`
for attaching sub-models) derive both a local C++ variable's name and its
`saiopt` lookup name from one identifier, so `model.var("bread")` later
doesn't need `"bread"` repeated as a separate string literal. `SAIOPT_OBJECTIVE`
rounds this family out as a thin wrapper around `set_objective` -- there's no
name to derive there, so it's only for keeping every declaration in a model
written the same way:

```cpp
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;

Model model;
SAIOPT_VAR(model, bread, VariableDef::continuous());
SAIOPT_VAR(model, milk, VariableDef::continuous());

SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);
SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);

auto solver = create_solver(model, "highs");
SolveResult result = solver->solve();
```

`create_solver(model, name)` looks up the backend registered under `name`
(linking `saiopt_highs` registers `"highs"`; writing your own backend is
just implementing `Solver` and registering it -- see `saiopt/solver.hpp`)
and translates `model` into that backend's native form once, handing back
a `Solver` you keep and call `solve()` on. Calling `solve()` again reuses
that same translated state rather than re-flattening the Block tree, so
it's cheap to solve the same model repeatedly. None of this needs a
HiGHS-specific header, so swapping backends never touches `Model` or the
rest of the modeling code.

`Solver` also carries coefficient/bound/kind get/set methods that used
to live on `Model` -- `objective_coefficient`/`set_objective_coefficient`,
`constraint_coefficient`/`set_constraint_coefficient`,
`constraint_lower`/`set_constraint_lower`,
`constraint_upper`/`set_constraint_upper`, `variable_lower`/
`set_variable_lower`, `variable_upper`/`set_variable_upper`, and
`variable_kind`/`set_variable_kind` -- but they now act directly on the
backend's live state (e.g. `HighsSolver` calls straight into HiGHS' own
change-coefficient API) instead of the `Model` tree, so a change takes
effect on the *next* `solve()` with no re-flattening either:

```cpp
auto solver = create_solver(model, "highs");
SolveResult r1 = solver->solve();

solver->set_objective_coefficient(bread, 4.0);  // reprice, no re-flatten
SolveResult r2 = solver->solve();
```

See `examples/solver_mutation.cpp` for all of these in one walkthrough.
