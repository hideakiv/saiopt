# saiopt
Structured Algebraic Interface for Optimization

A C++20 algebraic modeling library for LP/MILP, with [HiGHS](https://highs.dev/)
as the solver backend (linked via its C API).

## Building

```sh
cmake -S . -B build
cmake --build build -j
```

This fetches and builds HiGHS and Catch2 as part of the configure step.

Run the tests:

```sh
./build/tests/saiopt_tests
```

Run an example:

```sh
./build/examples/diet_problem
```

Other examples: `blocks_basics`, `indexed_macros`, `hierarchical_facilities`,
and `transportation` -- see `examples/` for what each demonstrates.

## Example

`SAIOPT_VAR`/`SAIOPT_CONSTRAINT` (and their indexed forms `SAIOPT_VARS`/
`SAIOPT_CONSTRAINTS`, plus `SAIOPT_BLOCK[_SCALED]`/`SAIOPT_BLOCKS[_SCALED]`
for attaching sub-models) derive both a local C++ variable's name and its
`saiopt` lookup name from one identifier, so `model.var("bread")` later
doesn't need `"bread"` repeated as a separate string literal:

```cpp
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"

using namespace saiopt;

Model model;
SAIOPT_VAR(model, bread, VariableDef::continuous());
SAIOPT_VAR(model, milk, VariableDef::continuous());

model.set_objective(ObjectiveSense::Minimize, 2.0 * bread + 3.0 * milk);
SAIOPT_CONSTRAINT(model, protein, 4.0 * bread + 8.0 * milk >= 20.0);
SAIOPT_CONSTRAINT(model, calories, 100.0 * bread + 150.0 * milk >= 400.0);

SolveResult result = model.solve();
```
