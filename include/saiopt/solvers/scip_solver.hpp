#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "saiopt/solver.hpp"
#include "saiopt/sparse_lp.hpp"

namespace saiopt {

// Solves a Model using SCIP, linked via its C API. Registers itself
// under the name "scip" (see solver.hpp), so create_solver(model,
// "scip") can build one without any SCIP-specific header.
//
// Constructing a ScipSolver flattens `model` and builds the equivalent
// SCIP problem once; solve() may then be called repeatedly. Unlike
// HighsSolver, a mutation after solve() (any of the coefficient/bound/
// kind setters below) needs SCIP back in its "problem" stage before it's
// legal, so those setters call SCIPfreeTransform() first if a solve has
// already happened -- SCIP itself (unlike HiGHS) then resolves cleanly
// from scratch on the next solve() with no leftover stuck state.
class ScipSolver : public Solver {
public:
    // Throws std::runtime_error if `model` has no objective, references
    // an out-of-scope variable (see flatten()), SCIP otherwise rejects
    // the flattened LP, or `options` sets a name SCIP doesn't recognize
    // or a value it rejects. `options`' keys are SCIP's own parameter
    // names (e.g. "limits/time", "limits/gap") -- see SCIP's own
    // documentation for the full list. `set_int` is tried against both
    // SCIP's int- and Longint-valued parameters, since saiopt's
    // SolverOptions doesn't distinguish them the way SCIP does.
    explicit ScipSolver(const Model& model, const SolverOptions& options = SolverOptions{});

    ScipSolver(ScipSolver&& other) noexcept;
    ScipSolver& operator=(ScipSolver&& other) noexcept;
    ScipSolver(const ScipSolver&) = delete;
    ScipSolver& operator=(const ScipSolver&) = delete;
    ~ScipSolver() override;

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

    void set_progress_callback(ProgressCallback callback) override;

    // SCIP picks the format from `path`'s extension, so on top of ".lp"
    // and ".mps" any format one of its default reader plugins can write
    // works too (e.g. ".cip", SCIP's own). An unrecognized extension
    // throws.
    void write_model(const std::string& path) const override;

    // Called by the SCIP event handler (src/solvers/scip_solver.cpp) -- not part
    // of the Solver interface, and not meant to be called directly, but
    // public for the same reason HighsSolver::invoke_progress_callback
    // is: the handler's callback signature is dictated by SCIP's own
    // plugin API rather than a member function. Returns false if
    // progress_callback_ (when set) requested the solve stop early --
    // the handler is what actually acts on that via SCIPinterruptSolve.
    bool invoke_progress_callback(const SolverProgress& progress) const;

private:
    std::int64_t column_of(Variable var) const;
    std::int64_t row_of(ConstraintRef cref) const;

    // Puts scip_ back in SCIP_STAGE_PROBLEM if a solve has moved it
    // past that -- required before any of SCIP's problem-mutation calls
    // (SCIPchgVarObj, SCIPchgCoefLinear, etc.), all of which fail outside
    // that stage.
    void ensure_mutable();

    // The coefficient at (row, col) as originally flattened -- 0.0 if
    // that pair isn't a structural nonzero in lp_'s matrix -- before any
    // override from set_constraint_coefficient.
    double flattened_coefficient(std::int64_t row, std::int64_t col) const;

    void* scip_ = nullptr;  // SCIP*, kept as void* so this header stays free of SCIP types
    SparseLp lp_;

    // Column/row -> native SCIP handle, and the reverse (built once in
    // the constructor) so Variable/ConstraintRef -> column/row lookups
    // are O(1). The vars/conss stay alive for scip_'s lifetime once
    // added to the problem, independent of our own SCIPrelease*() calls
    // right after creation (SCIP's own standard ownership idiom).
    std::vector<void*> scip_vars_;   // SCIP_VAR*
    std::vector<void*> scip_conss_;  // SCIP_CONS*
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
