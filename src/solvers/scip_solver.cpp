#include "saiopt/solvers/scip_solver.hpp"

#include <scip/scip.h>
#include <scip/scipdefplugins.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "saiopt/model.hpp"

namespace saiopt {

namespace {

SCIP* as_scip(void* p) { return static_cast<SCIP*>(p); }
SCIP_VAR* as_var(void* p) { return static_cast<SCIP_VAR*>(p); }
SCIP_CONS* as_cons(void* p) { return static_cast<SCIP_CONS*>(p); }

void check(SCIP_RETCODE rc, const char* what) {
    if (rc != SCIP_OKAY) {
        throw std::runtime_error(std::string("SCIP call failed (") + what + "): retcode " +
                                  std::to_string(static_cast<int>(rc)));
    }
}

SCIP_VARTYPE to_scip_vartype(VarKind kind) {
    switch (kind) {
        case VarKind::Continuous:
            return SCIP_VARTYPE_CONTINUOUS;
        case VarKind::Integer:
            return SCIP_VARTYPE_INTEGER;
        case VarKind::Binary:
            return SCIP_VARTYPE_BINARY;
    }
    return SCIP_VARTYPE_CONTINUOUS;
}

SolveStatus to_solve_status(SCIP_STATUS status) {
    switch (status) {
        case SCIP_STATUS_OPTIMAL:
            return SolveStatus::Optimal;
        case SCIP_STATUS_INFEASIBLE:
            return SolveStatus::Infeasible;
        case SCIP_STATUS_UNBOUNDED:
            return SolveStatus::Unbounded;
        case SCIP_STATUS_INFORUNBD:
            return SolveStatus::InfeasibleOrUnbounded;
        case SCIP_STATUS_TIMELIMIT:
            return SolveStatus::TimeLimit;
        case SCIP_STATUS_NODELIMIT:
        case SCIP_STATUS_TOTALNODELIMIT:
        case SCIP_STATUS_STALLNODELIMIT:
            return SolveStatus::NodeLimit;
        case SCIP_STATUS_SOLLIMIT:
        case SCIP_STATUS_BESTSOLLIMIT:
            return SolveStatus::SolutionLimit;
        case SCIP_STATUS_GAPLIMIT:
            return SolveStatus::GapLimit;
        case SCIP_STATUS_MEMLIMIT:
            return SolveStatus::MemoryLimit;
        case SCIP_STATUS_USERINTERRUPT:
        case SCIP_STATUS_TERMINATE:
            return SolveStatus::Interrupted;
        // Includes SCIP_STATUS_RESTARTLIMIT, which has no counterpart in
        // SolveStatus.
        default:
            return SolveStatus::Unknown;
    }
}

// SCIP treats any bound past its own infinity threshold as a numerics
// problem rather than as infinite unless it's exactly SCIPinfinity(),
// so saiopt's own kInfinity-based sentinels need translating both ways.
double to_scip_bound(SCIP* scip, double value) {
    if (value >= kInfinity) return SCIPinfinity(scip);
    if (value <= -kInfinity) return -SCIPinfinity(scip);
    return value;
}

double from_scip_bound(SCIP* scip, double value) {
    if (SCIPisInfinity(scip, value)) return kInfinity;
    if (SCIPisInfinity(scip, -value)) return -kInfinity;
    return value;
}

void check_option_status(SCIP_RETCODE rc, const std::string& key) {
    if (rc == SCIP_PARAMETERUNKNOWN) {
        throw std::runtime_error("SCIP does not recognize option \"" + key + "\"");
    }
    if (rc != SCIP_OKAY) {
        throw std::runtime_error("SCIP rejected option \"" + key + "\"");
    }
}

// Applies every option in `options` to `scip` via SCIP's own (segregated
// by type) parameter API, throwing if SCIP rejects any of them -- an
// unrecognized name or an invalid value for a recognized one. `int`
// options are tried against SCIP's int-valued parameters first, falling
// back to its separate Longint-valued ones on a type mismatch, since
// SolverOptions doesn't distinguish the two the way SCIP does.
void apply_options(SCIP* scip, const SolverOptions& options) {
    for (const auto& [key, value] : options.bool_options()) {
        check_option_status(SCIPsetBoolParam(scip, key.c_str(), value ? TRUE : FALSE), key);
    }
    for (const auto& [key, value] : options.int_options()) {
        SCIP_RETCODE rc = SCIPsetIntParam(scip, key.c_str(), static_cast<int>(value));
        if (rc == SCIP_PARAMETERWRONGTYPE) {
            rc = SCIPsetLongintParam(scip, key.c_str(), static_cast<SCIP_Longint>(value));
        }
        check_option_status(rc, key);
    }
    for (const auto& [key, value] : options.double_options()) {
        check_option_status(SCIPsetRealParam(scip, key.c_str(), value), key);
    }
    for (const auto& [key, value] : options.string_options()) {
        check_option_status(SCIPsetStringParam(scip, key.c_str(), value.c_str()), key);
    }
}

// The event handler that backs Solver::set_progress_callback: catches
// every node-solved/LP-solved event, reports a translated SolverProgress
// to whichever ScipSolver owns this SCIP instance, and asks SCIP to stop
// (via SCIPinterruptSolve) if that callback says to.
SCIP_DECL_EVENTEXEC(scip_progress_event_exec) {
    (void)eventhdlr;
    (void)event;
    (void)eventdata;
    auto* self = static_cast<ScipSolver*>(static_cast<void*>(SCIPeventhdlrGetData(eventhdlr)));

    SolverProgress progress;
    progress.elapsed_seconds = SCIPgetSolvingTime(scip);
    progress.objective_value = SCIPgetPrimalbound(scip);
    progress.iterations = SCIPgetNLPIterations(scip);
    progress.node_count = SCIPgetNNodes(scip);
    double dual_bound = SCIPgetDualbound(scip);
    if (!SCIPisInfinity(scip, std::abs(dual_bound))) progress.best_bound = dual_bound;
    if (!SCIPisInfinity(scip, std::abs(progress.objective_value))) {
        double gap = SCIPgetGap(scip);
        if (!SCIPisInfinity(scip, gap)) progress.gap = gap;
    }

    if (!self->invoke_progress_callback(progress)) {
        SCIPinterruptSolve(scip);
    }
    return SCIP_OKAY;
}

SCIP_DECL_EVENTINITSOL(scip_progress_event_initsol) {
    return SCIPcatchEvent(scip, SCIP_EVENTTYPE_NODESOLVED | SCIP_EVENTTYPE_LPEVENT, eventhdlr, nullptr,
                           nullptr);
}

SCIP_DECL_EVENTEXITSOL(scip_progress_event_exitsol) {
    return SCIPdropEvent(scip, SCIP_EVENTTYPE_NODESOLVED | SCIP_EVENTTYPE_LPEVENT, eventhdlr, nullptr, -1);
}

// How far outside a variable's bound an initial solution value may sit
// and still be accepted (and clamped back onto the bound) -- a solve's
// own values routinely land a hair past one, e.g. 1.0000000001 for a
// binary, so a previous SolveResult's values can be passed back in.
double bound_tolerance(double bound) { return 1e-6 * std::max(1.0, std::abs(bound)); }

}  // namespace

ScipSolver::ScipSolver(const Model& model, const SolverOptions& options) : lp_(flatten(model)) {
    SCIP* scip = nullptr;
    check(SCIPcreate(&scip), "SCIPcreate");
    scip_ = scip;

    try {
        check(SCIPincludeDefaultPlugins(scip), "SCIPincludeDefaultPlugins");
        SCIPsetMessagehdlrQuiet(scip, TRUE);
        apply_options(scip, options);

        check(SCIPcreateProbBasic(scip, "saiopt_model"), "SCIPcreateProbBasic");
        check(SCIPsetObjsense(scip, lp_.sense == 1 ? SCIP_OBJSENSE_MINIMIZE : SCIP_OBJSENSE_MAXIMIZE),
              "SCIPsetObjsense");
        if (lp_.offset != 0.0) check(SCIPaddOrigObjoffset(scip, lp_.offset), "SCIPaddOrigObjoffset");

        // One event handler drives set_progress_callback; SCIPgetEventhdlrData
        // (via eventhdlrdata below) is how the trampoline finds `this`.
        SCIP_EVENTHDLR* event_hdlr = nullptr;
        check(SCIPincludeEventhdlrBasic(scip, &event_hdlr, "saiopt_progress",
                                         "reports SolverProgress and honors interrupt requests",
                                         scip_progress_event_exec, reinterpret_cast<SCIP_EVENTHDLRDATA*>(this)),
              "SCIPincludeEventhdlrBasic");
        check(SCIPsetEventhdlrInitsol(scip, event_hdlr, scip_progress_event_initsol),
              "SCIPsetEventhdlrInitsol");
        check(SCIPsetEventhdlrExitsol(scip, event_hdlr, scip_progress_event_exitsol),
              "SCIPsetEventhdlrExitsol");

        std::vector<std::string> col_names = file_safe_names(lp_.col_name);
        std::vector<std::string> row_names = file_safe_names(lp_.row_name);

        std::size_t num_col = static_cast<std::size_t>(lp_.num_col);
        scip_vars_.reserve(num_col);
        for (std::size_t i = 0; i < num_col; ++i) {
            SCIP_VAR* var = nullptr;
            check(SCIPcreateVarBasic(scip, &var, col_names[i].c_str(),
                                      to_scip_bound(scip, lp_.col_lower[i]), to_scip_bound(scip, lp_.col_upper[i]),
                                      lp_.col_cost[i], to_scip_vartype(lp_.col_kind[i])),
                  "SCIPcreateVarBasic");
            check(SCIPaddVar(scip, var), "SCIPaddVar");
            scip_vars_.push_back(var);
            check(SCIPreleaseVar(scip, &var), "SCIPreleaseVar");
        }

        // SparseLp's matrix is column-major (CSC); transpose it once into
        // per-row (variable, coefficient) lists to build each constraint.
        std::size_t num_row = static_cast<std::size_t>(lp_.num_row);
        std::vector<std::vector<std::pair<std::int64_t, double>>> row_entries(num_row);
        for (std::size_t col = 0; col < num_col; ++col) {
            for (std::int64_t idx = lp_.a_start[col]; idx < lp_.a_start[col + 1]; ++idx) {
                std::size_t row = static_cast<std::size_t>(lp_.a_index[static_cast<std::size_t>(idx)]);
                row_entries[row].emplace_back(static_cast<std::int64_t>(col),
                                               lp_.a_value[static_cast<std::size_t>(idx)]);
            }
        }

        scip_conss_.reserve(num_row);
        std::vector<SCIP_VAR*> row_vars;
        std::vector<double> row_vals;
        for (std::size_t row = 0; row < num_row; ++row) {
            row_vars.clear();
            row_vals.clear();
            for (const auto& [col, coeff] : row_entries[row]) {
                row_vars.push_back(as_var(scip_vars_[static_cast<std::size_t>(col)]));
                row_vals.push_back(coeff);
            }
            SCIP_CONS* cons = nullptr;
            check(SCIPcreateConsBasicLinear(scip, &cons, row_names[row].c_str(),
                                             static_cast<int>(row_vars.size()), row_vars.data(), row_vals.data(),
                                             to_scip_bound(scip, lp_.row_lower[row]),
                                             to_scip_bound(scip, lp_.row_upper[row])),
                  "SCIPcreateConsBasicLinear");
            check(SCIPaddCons(scip, cons), "SCIPaddCons");
            scip_conss_.push_back(cons);
            check(SCIPreleaseCons(scip, &cons), "SCIPreleaseCons");
        }
    } catch (...) {
        // A throwing constructor's destructor never runs, so scip_ would
        // otherwise leak here -- clean it up ourselves before propagating.
        SCIPfree(&scip);
        scip_ = nullptr;
        throw;
    }

    for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
        var_column_.emplace(lp_.col_variable[i], static_cast<std::int64_t>(i));
    }
    for (std::size_t i = 0; i < lp_.row_constraint.size(); ++i) {
        constraint_row_.emplace(lp_.row_constraint[i], static_cast<std::int64_t>(i));
    }
}

ScipSolver::ScipSolver(ScipSolver&& other) noexcept
    : scip_(other.scip_),
      lp_(std::move(other.lp_)),
      scip_vars_(std::move(other.scip_vars_)),
      scip_conss_(std::move(other.scip_conss_)),
      var_column_(std::move(other.var_column_)),
      constraint_row_(std::move(other.constraint_row_)),
      coefficient_overrides_(std::move(other.coefficient_overrides_)),
      progress_callback_(std::move(other.progress_callback_)) {
    other.scip_ = nullptr;
    // The event handler's data is a raw `this` pointer, captured at
    // registration time -- refresh it to this (new) object's address, or
    // SCIP would keep calling back into the moved-from one.
    if (scip_) {
        SCIP_EVENTHDLR* event_hdlr = SCIPfindEventhdlr(as_scip(scip_), "saiopt_progress");
        if (event_hdlr) SCIPeventhdlrSetData(event_hdlr, reinterpret_cast<SCIP_EVENTHDLRDATA*>(this));
    }
}

ScipSolver& ScipSolver::operator=(ScipSolver&& other) noexcept {
    if (this != &other) {
        if (scip_) SCIPfree(reinterpret_cast<SCIP**>(&scip_));
        scip_ = other.scip_;
        lp_ = std::move(other.lp_);
        scip_vars_ = std::move(other.scip_vars_);
        scip_conss_ = std::move(other.scip_conss_);
        var_column_ = std::move(other.var_column_);
        constraint_row_ = std::move(other.constraint_row_);
        coefficient_overrides_ = std::move(other.coefficient_overrides_);
        progress_callback_ = std::move(other.progress_callback_);
        other.scip_ = nullptr;
        if (scip_) {
            SCIP_EVENTHDLR* event_hdlr = SCIPfindEventhdlr(as_scip(scip_), "saiopt_progress");
            if (event_hdlr) SCIPeventhdlrSetData(event_hdlr, reinterpret_cast<SCIP_EVENTHDLRDATA*>(this));
        }
    }
    return *this;
}

ScipSolver::~ScipSolver() {
    if (scip_) {
        SCIP* scip = as_scip(scip_);
        SCIPfree(&scip);
    }
}

void ScipSolver::ensure_mutable() {
    SCIP* scip = as_scip(scip_);
    if (SCIPgetStage(scip) != SCIP_STAGE_PROBLEM) {
        check(SCIPfreeTransform(scip), "SCIPfreeTransform");
    }
}

SolveResult ScipSolver::solve() {
    SCIP* scip = as_scip(scip_);
    check(SCIPsolve(scip), "SCIPsolve");

    SolveResult result;
    result.status = to_solve_status(SCIPgetStatus(scip));

    // SCIP only ever keeps feasible solutions, so having a best one at
    // all means there's a feasible primal solution to report.
    SCIP_SOL* sol = SCIPgetBestSol(scip);
    if (sol != nullptr) {
        result.has_solution = true;
        result.objective_value = SCIPgetSolOrigObj(scip, sol);
        for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
            result.variable_values.emplace(lp_.col_variable[i],
                                            SCIPgetSolVal(scip, sol, as_var(scip_vars_[i])));
        }
    }

    // Duals, reduced costs, and sensitivity ranging aren't populated for
    // ScipSolver yet -- getting them right (and only for a pure LP, the
    // way HighsSolver gates them) needs more verification against SCIP's
    // own LP-relaxation accessors than this backend has had so far, and
    // publishing an unverified value is worse than publishing none.

    return result;
}

void ScipSolver::write_model(const std::string& path) const {
    SCIP* scip = as_scip(scip_);
    // SCIPwriteOrigProblem opens (truncating) `path` before it looks for
    // a writer matching the extension, so check for one up front rather
    // than clobber an existing file and then fail. A trailing ".gz" is
    // SCIP's own compression suffix, not the format, so look past it.
    std::string stem = path;
    if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, ".gz") == 0) stem.resize(stem.size() - 3);
    std::size_t dot = stem.find_last_of('.');
    std::size_t slash = stem.find_last_of('/');
    std::string extension =
        (dot == std::string::npos || (slash != std::string::npos && dot < slash)) ? "" : stem.substr(dot + 1);
    SCIP_READER** readers = SCIPgetReaders(scip);
    bool writable = std::any_of(readers, readers + SCIPgetNReaders(scip), [&](SCIP_READER* reader) {
        return SCIPreaderCanWrite(reader) && extension == SCIPreaderGetExtension(reader);
    });
    if (!writable) {
        throw std::runtime_error("SCIP has no writer for the file format of \"" + path + "\"");
    }

    SCIP_RETCODE rc = SCIPwriteOrigProblem(scip, path.c_str(), nullptr, FALSE);
    if (rc != SCIP_OKAY) {
        throw std::runtime_error("SCIP could not write the model to \"" + path + "\" (retcode " +
                                 std::to_string(static_cast<int>(rc)) + ")");
    }
}

void ScipSolver::set_initial_solution(const std::unordered_map<Variable, double>& values) {
    if (values.empty()) return;
    std::vector<std::int64_t> columns;
    std::vector<double> col_values;
    columns.reserve(values.size());
    col_values.reserve(values.size());
    for (const auto& [var, value] : values) {
        std::int64_t col = column_of(var);
        std::size_t c = static_cast<std::size_t>(col);
        double lower = lp_.col_lower[c];
        double upper = lp_.col_upper[c];
        if (!std::isfinite(value) || value < lower - bound_tolerance(lower) ||
            value > upper + bound_tolerance(upper)) {
            throw std::runtime_error("initial solution value " + std::to_string(value) + " for variable \"" +
                                     lp_.col_name[c] + "\" is not finite or lies outside its bounds");
        }
        columns.push_back(static_cast<std::int64_t>(col));
        col_values.push_back(std::clamp(value, lower, upper));
    }

    // A solution added before solving is only checked (and, if
    // feasible, kept as an incumbent) once the solve starts. A partial
    // one -- not every variable given -- is instead handed to SCIP's
    // completesol heuristic, which tries to fill in the rest.
    ensure_mutable();
    SCIP* scip = as_scip(scip_);
    SCIP_SOL* sol = nullptr;
    bool partial = columns.size() < scip_vars_.size();
    if (partial) {
        check(SCIPcreatePartialSol(scip, &sol, nullptr), "SCIPcreatePartialSol");
    } else {
        check(SCIPcreateSol(scip, &sol, nullptr), "SCIPcreateSol");
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        SCIP_RETCODE rc = SCIPsetSolVal(scip, sol, as_var(scip_vars_[static_cast<std::size_t>(columns[i])]),
                                        col_values[i]);
        if (rc != SCIP_OKAY) {
            SCIPfreeSol(scip, &sol);
            check(rc, "SCIPsetSolVal");
        }
    }
    SCIP_Bool stored = FALSE;
    check(SCIPaddSolFree(scip, &sol, &stored), "SCIPaddSolFree");
}

std::int64_t ScipSolver::column_of(Variable var) const {
    auto it = var_column_.find(var);
    if (it == var_column_.end()) {
        throw std::runtime_error("variable does not belong to the model this Solver was built from");
    }
    return it->second;
}

std::int64_t ScipSolver::row_of(ConstraintRef cref) const {
    auto it = constraint_row_.find(cref);
    if (it == constraint_row_.end()) {
        throw std::runtime_error("constraint does not belong to the model this Solver was built from");
    }
    return it->second;
}

double ScipSolver::flattened_coefficient(std::int64_t row, std::int64_t col) const {
    auto begin = lp_.a_index.begin() + lp_.a_start[static_cast<std::size_t>(col)];
    auto end = lp_.a_index.begin() + lp_.a_start[static_cast<std::size_t>(col) + 1];
    auto it = std::lower_bound(begin, end, row);
    if (it == end || *it != row) return 0.0;
    return lp_.a_value[static_cast<std::size_t>(std::distance(lp_.a_index.begin(), it))];
}

double ScipSolver::objective_coefficient(Variable var) const {
    return lp_.col_cost[static_cast<std::size_t>(column_of(var))];
}

void ScipSolver::set_objective_coefficient(Variable var, double coeff) {
    std::int64_t col = column_of(var);
    ensure_mutable();
    check(SCIPchgVarObj(as_scip(scip_), as_var(scip_vars_[static_cast<std::size_t>(col)]), coeff),
          "SCIPchgVarObj");
    lp_.col_cost[static_cast<std::size_t>(col)] = coeff;
}

double ScipSolver::constraint_coefficient(ConstraintRef cref, Variable var) const {
    std::int64_t row = row_of(cref);
    std::int64_t col = column_of(var);
    auto it = coefficient_overrides_.find({row, col});
    if (it != coefficient_overrides_.end()) return it->second;
    return flattened_coefficient(row, col);
}

void ScipSolver::set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) {
    std::int64_t row = row_of(cref);
    std::int64_t col = column_of(var);
    ensure_mutable();
    check(SCIPchgCoefLinear(as_scip(scip_), as_cons(scip_conss_[static_cast<std::size_t>(row)]),
                             as_var(scip_vars_[static_cast<std::size_t>(col)]), coeff),
          "SCIPchgCoefLinear");
    coefficient_overrides_[{row, col}] = coeff;
}

double ScipSolver::constraint_lower(ConstraintRef cref) const {
    return lp_.row_lower[static_cast<std::size_t>(row_of(cref))];
}

void ScipSolver::set_constraint_lower(ConstraintRef cref, double lower) {
    std::int64_t row = row_of(cref);
    ensure_mutable();
    SCIP* scip = as_scip(scip_);
    check(SCIPchgLhsLinear(scip, as_cons(scip_conss_[static_cast<std::size_t>(row)]),
                            to_scip_bound(scip, lower)),
          "SCIPchgLhsLinear");
    lp_.row_lower[static_cast<std::size_t>(row)] = lower;
}

double ScipSolver::constraint_upper(ConstraintRef cref) const {
    return lp_.row_upper[static_cast<std::size_t>(row_of(cref))];
}

void ScipSolver::set_constraint_upper(ConstraintRef cref, double upper) {
    std::int64_t row = row_of(cref);
    ensure_mutable();
    SCIP* scip = as_scip(scip_);
    check(SCIPchgRhsLinear(scip, as_cons(scip_conss_[static_cast<std::size_t>(row)]),
                            to_scip_bound(scip, upper)),
          "SCIPchgRhsLinear");
    lp_.row_upper[static_cast<std::size_t>(row)] = upper;
}

double ScipSolver::variable_lower(Variable var) const {
    return lp_.col_lower[static_cast<std::size_t>(column_of(var))];
}

void ScipSolver::set_variable_lower(Variable var, double lower) {
    std::int64_t col = column_of(var);
    ensure_mutable();
    SCIP* scip = as_scip(scip_);
    check(SCIPchgVarLb(scip, as_var(scip_vars_[static_cast<std::size_t>(col)]), to_scip_bound(scip, lower)),
          "SCIPchgVarLb");
    lp_.col_lower[static_cast<std::size_t>(col)] = lower;
}

double ScipSolver::variable_upper(Variable var) const {
    return lp_.col_upper[static_cast<std::size_t>(column_of(var))];
}

void ScipSolver::set_variable_upper(Variable var, double upper) {
    std::int64_t col = column_of(var);
    ensure_mutable();
    SCIP* scip = as_scip(scip_);
    check(SCIPchgVarUb(scip, as_var(scip_vars_[static_cast<std::size_t>(col)]), to_scip_bound(scip, upper)),
          "SCIPchgVarUb");
    lp_.col_upper[static_cast<std::size_t>(col)] = upper;
}

VarKind ScipSolver::variable_kind(Variable var) const {
    return lp_.col_kind[static_cast<std::size_t>(column_of(var))];
}

void ScipSolver::set_variable_kind(Variable var, VarKind kind) {
    std::int64_t col = column_of(var);
    ensure_mutable();
    SCIP_Bool infeasible = FALSE;
    check(SCIPchgVarType(as_scip(scip_), as_var(scip_vars_[static_cast<std::size_t>(col)]),
                          to_scip_vartype(kind), &infeasible),
          "SCIPchgVarType");
    if (infeasible) {
        throw std::runtime_error(
            "changing this variable's kind conflicts with its current bounds (integrality infeasible)");
    }
    lp_.col_kind[static_cast<std::size_t>(col)] = kind;
}

void ScipSolver::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

bool ScipSolver::invoke_progress_callback(const SolverProgress& progress) const {
    if (!progress_callback_) return true;
    return progress_callback_(progress);
}

namespace {

SolverRegistration scip_registration{"scip", [](const Model& model, const SolverOptions& options) {
                                          return std::make_unique<ScipSolver>(model, options);
                                      }};

}  // namespace

}  // namespace saiopt
