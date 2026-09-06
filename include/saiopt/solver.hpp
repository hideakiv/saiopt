#pragma once

#include <functional>
#include <memory>
#include <string>

namespace saiopt {

class Model;
struct SolveResult;

// A solver instance bound to one Model. Constructing a concrete
// implementation (e.g. HighsSolver) translates that Model into the
// backend's native form once; solve() can then be called repeatedly
// without re-flattening the Block tree each time. saiopt's core library
// depends only on this interface, never on a concrete backend.
class Solver {
public:
    virtual ~Solver() = default;
    virtual SolveResult solve() = 0;
};

// A backend's constructor, wrapped so it can be looked up by name. See
// SolverRegistration.
using SolverFactory = std::function<std::unique_ptr<Solver>(const Model&)>;

// Registers `factory` under `name` for later lookup by create_solver.
// Most code shouldn't call this directly -- see SolverRegistration below.
void register_solver(std::string name, SolverFactory factory);

// Registers a backend's factory as the side effect of constructing a
// static instance, so a backend can make itself available under a name
// just by being linked in -- e.g., in that backend's own .cpp file:
//
//   namespace {
//   SolverRegistration registration{"highs", [](const Model& model) {
//       return std::make_unique<HighsSolver>(model);
//   }};
//   }
struct SolverRegistration {
    SolverRegistration(std::string name, SolverFactory factory);
};

// Builds a Solver for `model` using the backend registered under
// `solver_name` (e.g. "highs", registered by saiopt_highs when it's
// linked in). Throws std::runtime_error if no backend is registered
// under that name.
std::unique_ptr<Solver> create_solver(const Model& model, const std::string& solver_name);

}  // namespace saiopt
