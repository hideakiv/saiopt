#pragma once

#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

namespace saiopt {

// Solves a Model using HiGHS, linked via its C API.
class HighsSolver : public Solver {
public:
    SolveResult solve(const Model& model) const override;
};

}  // namespace saiopt
