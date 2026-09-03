#pragma once

#include "saiopt/model.hpp"

namespace saiopt {

// Solves a Model using HiGHS, linked via its C API.
class HighsSolver {
public:
    SolveResult solve(const Model& model) const;
};

}  // namespace saiopt
