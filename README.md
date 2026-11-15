# saiopt
Structured Algebraic Interface for Optimization

A modeling library for LP and MILP, with optional quadratic objectives,
for C++20 and Python. A model is a tree of **blocks**, each with its own
variables, constraints, and objective, which can be built independently,
even on separate MPI ranks, and then solved together with
[HiGHS](https://highs.dev/) or [SCIP](https://scipopt.org/).

Documentation is available [here](https://hideakiv.github.io/saiopt/):
step-by-step guides for [Python](https://hideakiv.github.io/saiopt/python/)
and [C++](https://hideakiv.github.io/saiopt/cpp/).

## Installation

Python 3.9+ (builds the C++ library, HiGHS, and SCIP into one extension
module; the first build takes a while):

```sh
pip install .          # add [mpi] for saiopt.mpi, [test] for pytest
```

C++ (needs CMake 3.21+; HiGHS, SCIP, and Catch2 are fetched and built
automatically, and MPI is needed unless `-DSAIOPT_WITH_MPI=OFF`):

```sh
cmake -S . -B build
cmake --build build -j
./build/tests/saiopt_tests
```

## Example

```python
import saiopt as so

model = so.Model()
bread = model.add_var("bread", bounds=(0, so.inf))
milk = model.add_var("milk", bounds=(0, so.inf))
model.add_constraint("protein", 4 * bread + 8 * milk >= 20)
model.add_constraint("calories", 100 * bread + 150 * milk >= 400)
model.set_objective(so.ObjectiveSense.Minimize, 2 * bread + 3 * milk)

result = so.create_solver(model, "highs").solve()
print(result.status, result.objective_value, result[bread], result[milk])
```

The same model in C++:

```cpp
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

using namespace saiopt;

Model model;
SAIOPT_VAR(model, bread, .bounds = {0.0, kInfinity});
SAIOPT_VAR(model, milk, .bounds = {0.0, kInfinity});
SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);
SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);

SolveResult result = create_solver(model, "highs")->solve();
```

More complete programs are in `examples/` (C++) and `python/examples/`.

## Performance

Time a modeling layer adds on top of the solver, measured from Python on
one LP from 60k to 1.92M variables: 24 regions, each an N x N
transportation sub-model, joined by one linking constraint. Each library
builds the model its intended way and solves it with HiGHS; the chart
shows wall time minus HiGHS's own logged run time, so solver speed drops
out. This is the subtraction linopy's
[JOSS paper](https://doi.org/10.21105/joss.04823) uses. Unlike the
zero-time-limit method of the [JuMP 1.0 paper](https://arxiv.org/abs/2206.03866)
and [PyOptInterface's benchmark](https://metab0t.github.io/PyOptInterface/benchmark.html),
every run here solves for real, so the time includes copying the
solution back, and the numbers aren't directly comparable with theirs.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark-overhead-dark.svg">
  <img alt="Log-log chart of time spent outside HiGHS against number of variables, from 60k to 1.92M, for seven setups; the values are in the table below."
       src="docs/benchmark-overhead-light.svg">
</picture>

Time outside HiGHS, in seconds (median of 2 runs):

| Setup | 60k | 121k | 240k | 477k | 960k | 1.92M |
|---|---:|---:|---:|---:|---:|---:|
| saiopt, blocks, 6 MPI ranks | 0.12 | 0.23 | 0.45 | 0.86 | 1.84 | 4.26 |
| saiopt, blocks, 1 process | 0.17 | 0.34 | 0.71 | 1.43 | 3.01 | 6.70 |
| saiopt, flat | 0.21 | 0.47 | 0.94 | 2.12 | 4.51 | 9.94 |
| linopy | 0.08 | 0.10 | 0.14 | 0.23 | 0.37 | 0.68 |
| PyOptInterface | 0.15 | 0.30 | 0.62 | 1.32 | 2.75 | 6.06 |
| PuLP | 0.42 | 0.91 | 1.95 | 4.07 | 8.70 | 18.41 |
| Pyomo | 0.76 | 1.61 | 3.37 | 6.95 | 14.84 | 32.29 |

- **saiopt, flat** builds the whole model in one `Model`, keyed by
  `(k, i, j)`. **saiopt, blocks** builds each region as its own `Block`
  keyed by `(i, j)` and attaches it -- the same LP, and faster even in a
  single process, because each indexed group stays small. With
  `saiopt.mpi`, ranks build their regions in parallel and rank 0 gathers
  them before solving.
- linopy builds from whole arrays rather than one expression per
  constraint, so it scales differently from everything else here;
  saiopt's place is between it and the expression-based libraries, with
  composable blocks, name-based lookup, and MPI on top.
- Not included: highspy, which is HiGHS's own interface rather than a
  solver-independent modeling library, and python-mip, which can't use
  HiGHS.

Measured on an AMD Ryzen 9 9900X (12 cores) under WSL2, Python 3.12, with
HiGHS 1.15.1 (saiopt, PyOptInterface) or 1.16.0 (the rest), linopy 0.10,
PyOptInterface 0.6, PuLP 4.2, and Pyomo 6.10 (its appsi HiGHS
interface). HiGHS logs its run time to the nearest 10 ms, so the smallest
sizes carry a little rounding noise. To reproduce, with saiopt installed
as `saiopt[mpi]` alongside those libraries (and matplotlib, for the chart):

```sh
cd benchmarks/python
python run.py      # writes results.csv; see --help for sizes, ranks, reps
python plot.py     # redraws docs/benchmark-overhead-{light,dark}.svg, prints the table
```
