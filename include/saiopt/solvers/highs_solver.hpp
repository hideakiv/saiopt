#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

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
// Block tree on every call. The coefficient/bound accessors act directly
// on that live HiGHS instance (via its change-coefficient C API), so
// they too skip re-flattening.
class HighsSolver : public Solver {
public:
    // Throws std::runtime_error if `model` has no objective, references
    // an out-of-scope variable (see flatten()), HiGHS otherwise rejects
    // the flattened LP, or `options` sets a name HiGHS doesn't recognize
    // or a value it rejects. `options`' keys are HiGHS' own option
    // names (e.g. "time_limit", "mip_rel_gap", "presolve", "threads") --
    // see HiGHS' own documentation for the full list -- applied after
    // saiopt's own default of suppressing solve output ("output_flag"),
    // so an explicit output_flag in `options` overrides that default.
    explicit HighsSolver(const Model& model, const SolverOptions& options = SolverOptions{});

    HighsSolver(HighsSolver&& other) noexcept;
    HighsSolver& operator=(HighsSolver&& other) noexcept;
    HighsSolver(const HighsSolver&) = delete;
    HighsSolver& operator=(const HighsSolver&) = delete;
    ~HighsSolver() override;

    SolveResult solve() override;

    double objective_coefficient(Variable var) const override;
    void set_objective_coefficient(Variable var, double coeff) override;

    double constraint_coefficient(ConstraintRef cref, Variable var) const override;
    void set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) override;

    double constraint_lower(ConstraintRef cref) const override;
    void set_constraint_lower(ConstraintRef cref, double lower) override;

    double constraint_upper(ConstraintRef cref) const override;
    void set_constraint_upper(ConstraintRef cref, double upper) override;

    double variable_lower(Variable var) const override;
    void set_variable_lower(Variable var, double lower) override;

    double variable_upper(Variable var) const override;
    void set_variable_upper(Variable var, double upper) override;

    VarKind variable_kind(Variable var) const override;
    void set_variable_kind(Variable var, VarKind kind) override;

    // Fires from HiGHS' simplex, IPM, and MIP branch-and-bound progress
    // events (the *Interrupt-flavored ones, since those are the only
    // ones that can actually honor an interrupt request). Once solve()
    // has been interrupted this way, HiGHS leaves this instance's model
    // status stuck reporting the interrupt on every subsequent solve()
    // -- neither clearing the callback nor HiGHS' own solver/model reset
    // calls have been found to un-stick it; build a new Solver via
    // create_solver for a clean re-attempt.
    void set_progress_callback(ProgressCallback callback) override;

    // HiGHS picks the format from `path`'s extension: ".lp" or ".mps".
    // An unrecognized extension throws.
    void write_model(const std::string& path) const override;

    void set_initial_solution(const std::unordered_map<Variable, double>& values) override;

    // Called by the HiGHS callback trampoline (src/solvers/highs_solver.cpp) --
    // not part of the Solver interface, and not meant to be called
    // directly, but the trampoline is a free function (its signature is
    // dictated by HiGHS' C callback type) rather than a member, so this
    // has to be public rather than a friend without leaking HiGHS'
    // callback struct types into this header. Returns false if
    // progress_callback_ (when set) requested the solve stop early --
    // the trampoline is what actually acts on that by setting HiGHS' own
    // interrupt flag.
    bool invoke_progress_callback(const SolverProgress& progress) const;

private:
    std::int64_t column_of(Variable var) const;
    std::int64_t row_of(ConstraintRef cref) const;

    // The coefficient at (row, col) as originally flattened -- 0.0 if
    // that pair isn't a structural nonzero in lp_'s matrix -- before any
    // override from set_constraint_coefficient.
    double flattened_coefficient(std::int64_t row, std::int64_t col) const;

    void* highs_ = nullptr;
    SparseLp lp_;

    // Reverse of lp_.col_variable / lp_.row_constraint, built once so
    // Variable/ConstraintRef -> column/row lookups are O(1).
    std::unordered_map<Variable, std::int64_t> var_column_;
    std::unordered_map<ConstraintRef, std::int64_t> constraint_row_;

    // Coefficients changed after construction via set_constraint_coefficient,
    // keyed by (row, col) -- overlaid on lp_'s original matrix so
    // constraint_coefficient() stays correct even for a pair that was a
    // structural zero (absent from the matrix) at flatten time.
    std::map<std::pair<std::int64_t, std::int64_t>, double> coefficient_overrides_;

    ProgressCallback progress_callback_;
};

}  // namespace saiopt
