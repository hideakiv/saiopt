#pragma once

namespace saiopt {

class Model;
struct SolveResult;

// Abstract interface for a backend capable of solving a Model. saiopt's
// core library depends only on this interface, never on a concrete
// backend, so that swapping solvers (or adding a new one) never touches
// core code. Concrete implementations, e.g. HighsSolver, live in their
// own headers/libraries.
class Solver {
public:
    virtual ~Solver() = default;
    virtual SolveResult solve(const Model& model) const = 0;
};

}  // namespace saiopt
