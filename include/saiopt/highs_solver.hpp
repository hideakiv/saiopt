#pragma once

#include "saiopt/solver.hpp"
#include "saiopt/sparse_lp.hpp"

namespace saiopt {

// Solves a Model using HiGHS, linked via its C API. Registers itself
// under the name "highs" (see solver.hpp), so create_solver(model,
// "highs") can build one without any HiGHS-specific header.
//
// Constructing a HighsSolver flattens `model` and passes it to HiGHS
// once; solve() may then be called repeatedly, re-running HiGHS against
// that same translated state each time rather than re-flattening the
// Block tree on every call.
class HighsSolver : public Solver {
public:
    // Throws std::runtime_error if `model` has no objective, references
    // an out-of-scope variable (see flatten()), or HiGHS otherwise
    // rejects the flattened LP.
    explicit HighsSolver(const Model& model);

    HighsSolver(HighsSolver&& other) noexcept;
    HighsSolver& operator=(HighsSolver&& other) noexcept;
    HighsSolver(const HighsSolver&) = delete;
    HighsSolver& operator=(const HighsSolver&) = delete;
    ~HighsSolver() override;

    SolveResult solve() override;

private:
    void* highs_ = nullptr;
    SparseLp lp_;
};

}  // namespace saiopt
