#include "saiopt/solver.hpp"

#include <stdexcept>
#include <unordered_map>

namespace saiopt {

namespace {

// Function-local static so the registry is guaranteed initialized before
// first use regardless of static-init order across translation units --
// backends register themselves from their own static-init time.
std::unordered_map<std::string, SolverFactory>& registry() {
    static std::unordered_map<std::string, SolverFactory> instance;
    return instance;
}

}  // namespace

void register_solver(std::string name, SolverFactory factory) {
    registry()[std::move(name)] = std::move(factory);
}

SolverRegistration::SolverRegistration(std::string name, SolverFactory factory) {
    register_solver(std::move(name), std::move(factory));
}

std::unique_ptr<Solver> create_solver(const Model& model, const std::string& solver_name) {
    auto it = registry().find(solver_name);
    if (it == registry().end()) {
        throw std::runtime_error("no solver registered under name '" + solver_name + "'");
    }
    return it->second(model);
}

}  // namespace saiopt
