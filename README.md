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

## Example

```cpp
#include "saiopt/model.hpp"

using namespace saiopt;

Model model;
Variable bread = model.add_variable("bread");
Variable milk = model.add_variable("milk");

model.set_objective(2.0 * bread + 3.0 * milk, ObjectiveSense::Minimize);
model.add_constraint(4.0 * bread + 8.0 * milk >= 20.0);
model.add_constraint(100.0 * bread + 150.0 * milk >= 400.0);

SolveResult result = model.solve();
```
