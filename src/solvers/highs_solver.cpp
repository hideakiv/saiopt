#include "saiopt/solvers/highs_solver.hpp"

#include <interfaces/highs_c_api.h>

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

std::vector<HighsInt> to_highs_int(const std::vector<std::int64_t>& values) {
    std::vector<HighsInt> result(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        result[i] = static_cast<HighsInt>(values[i]);
    }
    return result;
}

bool is_pure_lp(const SparseLp& lp) {
    return std::all_of(lp.col_kind.begin(), lp.col_kind.end(),
                        [](VarKind kind) { return kind == VarKind::Continuous; });
}

// HiGHS solves continuous QPs only -- given a quadratic objective with
// any integer column, it returns no solution and an unknown status --
// so refuse that combination up front with a message that says why.
[[noreturn]] void throw_unsupported_miqp() {
    throw std::runtime_error(
        "HiGHS can't solve a quadratic objective with integer or binary variables; "
        "use the \"scip\" backend instead");
}

// HiGHS has no separate "binary" integrality -- a binary variable is just
// an integer one with [0, 1] bounds, so saiopt's own VarKind (which does
// distinguish them, for lp_.col_kind and is_pure_lp) collapses to this.
HighsInt to_highs_integrality(VarKind kind) {
    return kind == VarKind::Continuous ? kHighsVarTypeContinuous : kHighsVarTypeInteger;
}

SolveStatus to_solve_status(HighsInt model_status) {
    switch (model_status) {
        // A model with no columns has nothing to optimize, so it's
        // trivially solved at its constant objective.
        case kHighsModelStatusOptimal:
        case kHighsModelStatusModelEmpty:
            return SolveStatus::Optimal;
        case kHighsModelStatusInfeasible:
            return SolveStatus::Infeasible;
        case kHighsModelStatusUnbounded:
            return SolveStatus::Unbounded;
        case kHighsModelStatusUnboundedOrInfeasible:
            return SolveStatus::InfeasibleOrUnbounded;
        case kHighsModelStatusTimeLimit:
            return SolveStatus::TimeLimit;
        case kHighsModelStatusIterationLimit:
            return SolveStatus::IterationLimit;
        case kHighsModelStatusSolutionLimit:
            return SolveStatus::SolutionLimit;
        case kHighsModelStatusObjectiveBound:
        case kHighsModelStatusObjectiveTarget:
            return SolveStatus::ObjectiveLimit;
        case kHighsModelStatusInterrupt:
            return SolveStatus::Interrupted;
        case kHighsModelStatusLoadError:
        case kHighsModelStatusModelError:
        case kHighsModelStatusPresolveError:
        case kHighsModelStatusSolveError:
        case kHighsModelStatusPostsolveError:
            return SolveStatus::Error;
        default:
            return SolveStatus::Unknown;
    }
}

// The trampoline HiGHS calls for every event type HighsSolver starts
// listening for; `user_data` is the HighsSolver instance passed to
// Highs_setCallback. Builds a solver-agnostic SolverProgress from
// whichever HighsCallbackDataOut fields apply to `callback_type`, and
// sets HiGHS' own interrupt flag if the registered callback (if any)
// asked to stop.
void highs_progress_trampoline(int callback_type, const char* /*message*/,
                                const HighsCallbackDataOut* data_out, HighsCallbackDataIn* data_in,
                                void* user_data) {
    auto* self = static_cast<HighsSolver*>(user_data);

    SolverProgress progress;
    progress.elapsed_seconds = data_out->running_time;
    progress.objective_value = data_out->objective_function_value;
    if (callback_type == kHighsCallbackMipInterrupt) {
        progress.iterations = data_out->mip_total_lp_iterations;
        progress.best_bound = data_out->mip_dual_bound;
        progress.gap = data_out->mip_gap;
        progress.node_count = data_out->mip_node_count;
    } else if (callback_type == kHighsCallbackIpmInterrupt) {
        progress.iterations = data_out->ipm_iteration_count;
    } else {
        progress.iterations = data_out->simplex_iteration_count;
    }

    if (!self->invoke_progress_callback(progress)) {
        data_in->user_interrupt = 1;
    }
}

// Registers the trampoline above and starts listening for the event
// types that can actually honor an interrupt request (unlike, say, the
// logging-flavored events) -- covers LP simplex, LP IPM, and MIP
// branch-and-bound progress in one shot.
void register_progress_callback(void* highs, HighsSolver* self) {
    HighsInt status = Highs_setCallback(highs, &highs_progress_trampoline, self);
    if (status != kHighsStatusOk) {
        throw std::runtime_error("HiGHS rejected the progress callback registration");
    }
}

// The event types the trampoline handles are only started while a
// progress callback is actually set: HiGHS does measurable work for every
// started callback event even when the trampoline then does nothing (on a
// 1M-column LP, about a third of a second per solve).
const HighsInt kProgressCallbackTypes[] = {kHighsCallbackSimplexInterrupt, kHighsCallbackIpmInterrupt,
                                           kHighsCallbackMipInterrupt};

void check_option_status(HighsInt status, const std::string& key) {
    if (status != kHighsStatusOk) {
        throw std::runtime_error("HiGHS rejected option \"" + key + "\"");
    }
}

// Applies every option in `options` to `highs` via HiGHS' own (segregated
// by type) option-setting API, throwing if HiGHS rejects any of them --
// an unrecognized name or an invalid value for a recognized one.
void apply_options(void* highs, const SolverOptions& options) {
    for (const auto& [key, value] : options.bool_options()) {
        check_option_status(Highs_setBoolOptionValue(highs, key.c_str(), value ? 1 : 0), key);
    }
    for (const auto& [key, value] : options.int_options()) {
        check_option_status(Highs_setIntOptionValue(highs, key.c_str(), static_cast<HighsInt>(value)),
                             key);
    }
    for (const auto& [key, value] : options.double_options()) {
        check_option_status(Highs_setDoubleOptionValue(highs, key.c_str(), value), key);
    }
    for (const auto& [key, value] : options.string_options()) {
        check_option_status(Highs_setStringOptionValue(highs, key.c_str(), value.c_str()), key);
    }
}

// How far outside a variable's bound an initial solution value may sit
// and still be accepted (and clamped back onto the bound) -- a solve's
// own values routinely land a hair past one, e.g. 1.0000000001 for a
// binary, so a previous SolveResult's values can be passed back in.
double bound_tolerance(double bound) { return 1e-6 * std::max(1.0, std::abs(bound)); }

}  // namespace

HighsSolver::HighsSolver(const Model& model, const SolverOptions& options) : lp_(flatten(model)) {
    if (lp_.has_quadratic_objective() && !is_pure_lp(lp_)) throw_unsupported_miqp();
    if (lp_.has_sos2()) {
        throw std::runtime_error("HiGHS doesn't support SOS2 sets, and the model has one (\"" + lp_.sos2_name.front() +
                                 "\"); use the \"scip\" backend instead, or, for a piecewise linear "
                                 "function, PiecewiseMethod::Incremental");
    }
    highs_ = Highs_create();
    try {
        Highs_setBoolOptionValue(highs_, "output_flag", 0);
        apply_options(highs_, options);

        std::vector<HighsInt> a_start = to_highs_int(lp_.a_start);
        std::vector<HighsInt> a_index = to_highs_int(lp_.a_index);

        HighsInt status = Highs_passLp(
            highs_, static_cast<HighsInt>(lp_.num_col), static_cast<HighsInt>(lp_.num_row),
            static_cast<HighsInt>(lp_.a_value.size()), kHighsMatrixFormatColwise, lp_.sense,
            lp_.offset, lp_.col_cost.data(), lp_.col_lower.data(), lp_.col_upper.data(),
            lp_.row_lower.data(), lp_.row_upper.data(), a_start.data(), a_index.data(),
            lp_.a_value.data());

        if (status != 0) {
            throw std::runtime_error("HiGHS rejected the model (Highs_passLp failed)");
        }

        if (lp_.has_quadratic_objective()) {
            // HiGHS takes q_start without SparseLp's trailing end marker.
            std::vector<HighsInt> q_start = to_highs_int(lp_.q_start);
            q_start.pop_back();
            std::vector<HighsInt> q_index = to_highs_int(lp_.q_index);
            if (Highs_passHessian(highs_, static_cast<HighsInt>(lp_.num_col),
                                  static_cast<HighsInt>(lp_.q_value.size()), kHighsHessianFormatTriangular,
                                  q_start.data(), q_index.data(), lp_.q_value.data()) == kHighsStatusError) {
                throw std::runtime_error("HiGHS rejected the quadratic objective (Highs_passHessian failed)");
            }
        }

        for (std::size_t i = 0; i < lp_.col_kind.size(); ++i) {
            if (lp_.col_kind[i] != VarKind::Continuous) {
                Highs_changeColIntegrality(highs_, static_cast<HighsInt>(i), kHighsVarTypeInteger);
            }
        }

        register_progress_callback(highs_, this);
    } catch (...) {
        // A throwing constructor's destructor never runs, so highs_ would
        // otherwise leak here -- clean it up ourselves before propagating.
        Highs_destroy(highs_);
        throw;
    }

    for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
        var_column_.emplace(lp_.col_variable[i], static_cast<std::int64_t>(i));
    }
    for (std::size_t i = 0; i < lp_.row_constraint.size(); ++i) {
        constraint_row_.emplace(lp_.row_constraint[i], static_cast<std::int64_t>(i));
    }
}

HighsSolver::HighsSolver(HighsSolver&& other) noexcept
    : highs_(other.highs_),
      lp_(std::move(other.lp_)),
      var_column_(std::move(other.var_column_)),
      constraint_row_(std::move(other.constraint_row_)),
      coefficient_overrides_(std::move(other.coefficient_overrides_)),
      progress_callback_(std::move(other.progress_callback_)),
      sensitivity_ranging_(other.sensitivity_ranging_),
      names_passed_(other.names_passed_) {
    other.highs_ = nullptr;
    // Highs_setCallback's user_data is a raw `this` pointer, captured at
    // registration time -- refresh it to this (new) object's address, or
    // HiGHS would keep calling back into the moved-from one.
    if (highs_) Highs_setCallback(highs_, &highs_progress_trampoline, this);
}

HighsSolver& HighsSolver::operator=(HighsSolver&& other) noexcept {
    if (this != &other) {
        if (highs_) Highs_destroy(highs_);
        highs_ = other.highs_;
        lp_ = std::move(other.lp_);
        var_column_ = std::move(other.var_column_);
        constraint_row_ = std::move(other.constraint_row_);
        coefficient_overrides_ = std::move(other.coefficient_overrides_);
        progress_callback_ = std::move(other.progress_callback_);
        sensitivity_ranging_ = other.sensitivity_ranging_;
        names_passed_ = other.names_passed_;
        other.highs_ = nullptr;
        if (highs_) Highs_setCallback(highs_, &highs_progress_trampoline, this);
    }
    return *this;
}

HighsSolver::~HighsSolver() {
    if (highs_) Highs_destroy(highs_);
}

SolveResult HighsSolver::solve() {
    Highs_run(highs_);

    SolveResult result;
    HighsInt model_status = Highs_getModelStatus(highs_);
    result.status = to_solve_status(model_status);

    // HiGHS fills the solution arrays even when what's in them isn't a
    // feasible point (e.g. after an interrupted simplex), so gate on its
    // own primal solution status rather than on the model status alone --
    // a MIP stopped by a limit can still hold a feasible incumbent. An
    // empty model is the exception: HiGHS reports no primal solution for
    // it, but the empty assignment is trivially feasible.
    HighsInt primal_solution_status = kHighsSolutionStatusNone;
    Highs_getIntInfoValue(highs_, "primal_solution_status", &primal_solution_status);
    result.has_solution = (primal_solution_status == kHighsSolutionStatusFeasible ||
                           model_status == kHighsModelStatusModelEmpty);

    std::vector<double> col_value(lp_.num_col);
    std::vector<double> row_value(lp_.num_row);
    std::vector<double> col_dual(lp_.num_col);
    std::vector<double> row_dual(lp_.num_row);
    Highs_getSolution(highs_, col_value.data(), col_dual.data(), row_value.data(), row_dual.data());

    if (result.has_solution) {
        for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
            result.variable_values.emplace(lp_.col_variable[i], col_value[i]);
        }
        result.objective_value = Highs_getObjectiveValue(highs_);
    }

    // HiGHS only produces meaningful duals/reduced costs for a model with
    // no integer/binary column (an LP, or a continuous QP, whose duals
    // are its KKT multipliers): for a MILP the dual arrays from
    // Highs_getSolution come back all zero (not the final LP relaxation's
    // values), so leave these unset rather than publish misleading zeros.
    // Ranging is a property of an LP basis, so a QP doesn't get it either,
    // and it's only computed when asked for (see set_sensitivity_ranging).
    if (result.optimal() && is_pure_lp(lp_)) {
        for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
            result.reduced_costs.emplace(lp_.col_variable[i], col_dual[i]);
        }
        for (std::size_t i = 0; i < lp_.row_constraint.size(); ++i) {
            result.duals.emplace(lp_.row_constraint[i], row_dual[i]);
        }
        if (sensitivity_ranging_ && !lp_.has_quadratic_objective()) {
            std::vector<double> col_cost_up(lp_.num_col);
            std::vector<double> col_cost_dn(lp_.num_col);
            std::vector<double> col_bound_up(lp_.num_col);
            std::vector<double> col_bound_dn(lp_.num_col);
            std::vector<double> row_bound_up(lp_.num_row);
            std::vector<double> row_bound_dn(lp_.num_row);

            HighsInt ranging_status = Highs_getRanging(
                highs_, col_cost_up.data(), nullptr, nullptr, nullptr, col_cost_dn.data(), nullptr,
                nullptr, nullptr, col_bound_up.data(), nullptr, nullptr, nullptr,
                col_bound_dn.data(), nullptr, nullptr, nullptr, row_bound_up.data(), nullptr,
                nullptr, nullptr, row_bound_dn.data(), nullptr, nullptr, nullptr);

            if (ranging_status == kHighsStatusOk) {
                for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
                    result.objective_coefficient_ranges.emplace(
                        lp_.col_variable[i], SensitivityRange{col_cost_dn[i], col_cost_up[i]});
                    result.variable_bound_ranges.emplace(
                        lp_.col_variable[i], SensitivityRange{col_bound_dn[i], col_bound_up[i]});
                }
                for (std::size_t i = 0; i < lp_.row_constraint.size(); ++i) {
                    result.constraint_bound_ranges.emplace(
                        lp_.row_constraint[i], SensitivityRange{row_bound_dn[i], row_bound_up[i]});
                }
            }
        }
    }

    return result;
}

void HighsSolver::write_model(const std::string& path) const {
    // Names are only handed to HiGHS here, the first time they're needed:
    // HiGHS spends time on them at the start of every run otherwise (on a
    // 1M-column LP, about a fifth of a second per solve, on top of passing
    // them in), and nothing but a written file uses them.
    if (!names_passed_) {
        std::vector<std::string> col_names = file_safe_names(lp_.col_name);
        for (std::size_t i = 0; i < col_names.size(); ++i) {
            Highs_passColName(highs_, static_cast<HighsInt>(i), col_names[i].c_str());
        }
        std::vector<std::string> row_names = file_safe_names(lp_.row_name);
        for (std::size_t i = 0; i < row_names.size(); ++i) {
            Highs_passRowName(highs_, static_cast<HighsInt>(i), row_names[i].c_str());
        }
        names_passed_ = true;
    }
    if (Highs_writeModel(highs_, path.c_str()) == kHighsStatusError) {
        throw std::runtime_error("HiGHS could not write the model to \"" + path + "\"");
    }
}

void HighsSolver::set_initial_solution(const std::unordered_map<Variable, double>& values) {
    if (values.empty()) return;
    std::vector<HighsInt> columns;
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
        columns.push_back(static_cast<HighsInt>(col));
        col_values.push_back(std::clamp(value, lower, upper));
    }

    // HiGHS fills in the variables left out itself -- completing the
    // discrete ones and then solving for the rest -- at the start of the
    // next run.
    if (Highs_setSparseSolution(highs_, static_cast<HighsInt>(columns.size()), columns.data(),
                                col_values.data()) == kHighsStatusError) {
        throw std::runtime_error("HiGHS rejected the initial solution");
    }
}

std::int64_t HighsSolver::column_of(Variable var) const {
    auto it = var_column_.find(var);
    if (it == var_column_.end()) {
        throw std::runtime_error("variable does not belong to the model this Solver was built from");
    }
    return it->second;
}

std::int64_t HighsSolver::row_of(ConstraintRef cref) const {
    auto it = constraint_row_.find(cref);
    if (it == constraint_row_.end()) {
        throw std::runtime_error("constraint does not belong to the model this Solver was built from");
    }
    return it->second;
}

double HighsSolver::flattened_coefficient(std::int64_t row, std::int64_t col) const {
    auto begin = lp_.a_index.begin() + lp_.a_start[static_cast<std::size_t>(col)];
    auto end = lp_.a_index.begin() + lp_.a_start[static_cast<std::size_t>(col) + 1];
    auto it = std::lower_bound(begin, end, row);
    if (it == end || *it != row) return 0.0;
    return lp_.a_value[static_cast<std::size_t>(std::distance(lp_.a_index.begin(), it))];
}

double HighsSolver::objective_coefficient(Variable var) const {
    return lp_.col_cost[static_cast<std::size_t>(column_of(var))];
}

void HighsSolver::set_objective_coefficient(Variable var, double coeff) {
    std::int64_t col = column_of(var);
    Highs_changeColCost(highs_, static_cast<HighsInt>(col), coeff);
    lp_.col_cost[static_cast<std::size_t>(col)] = coeff;
}

double HighsSolver::constraint_coefficient(ConstraintRef cref, Variable var) const {
    std::int64_t row = row_of(cref);
    std::int64_t col = column_of(var);
    auto it = coefficient_overrides_.find({row, col});
    if (it != coefficient_overrides_.end()) return it->second;
    return flattened_coefficient(row, col);
}

void HighsSolver::set_constraint_coefficient(ConstraintRef cref, Variable var, double coeff) {
    std::int64_t row = row_of(cref);
    std::int64_t col = column_of(var);
    Highs_changeCoeff(highs_, static_cast<HighsInt>(row), static_cast<HighsInt>(col), coeff);
    coefficient_overrides_[{row, col}] = coeff;
}

double HighsSolver::constraint_lower(ConstraintRef cref) const {
    return lp_.row_lower[static_cast<std::size_t>(row_of(cref))];
}

void HighsSolver::set_constraint_lower(ConstraintRef cref, double lower) {
    std::int64_t row = row_of(cref);
    Highs_changeRowBounds(highs_, static_cast<HighsInt>(row), lower,
                           lp_.row_upper[static_cast<std::size_t>(row)]);
    lp_.row_lower[static_cast<std::size_t>(row)] = lower;
}

double HighsSolver::constraint_upper(ConstraintRef cref) const {
    return lp_.row_upper[static_cast<std::size_t>(row_of(cref))];
}

void HighsSolver::set_constraint_upper(ConstraintRef cref, double upper) {
    std::int64_t row = row_of(cref);
    Highs_changeRowBounds(highs_, static_cast<HighsInt>(row),
                           lp_.row_lower[static_cast<std::size_t>(row)], upper);
    lp_.row_upper[static_cast<std::size_t>(row)] = upper;
}

double HighsSolver::variable_lower(Variable var) const {
    return lp_.col_lower[static_cast<std::size_t>(column_of(var))];
}

void HighsSolver::set_variable_lower(Variable var, double lower) {
    std::int64_t col = column_of(var);
    Highs_changeColBounds(highs_, static_cast<HighsInt>(col), lower,
                           lp_.col_upper[static_cast<std::size_t>(col)]);
    lp_.col_lower[static_cast<std::size_t>(col)] = lower;
}

double HighsSolver::variable_upper(Variable var) const {
    return lp_.col_upper[static_cast<std::size_t>(column_of(var))];
}

void HighsSolver::set_variable_upper(Variable var, double upper) {
    std::int64_t col = column_of(var);
    Highs_changeColBounds(highs_, static_cast<HighsInt>(col),
                           lp_.col_lower[static_cast<std::size_t>(col)], upper);
    lp_.col_upper[static_cast<std::size_t>(col)] = upper;
}

VarKind HighsSolver::variable_kind(Variable var) const {
    return lp_.col_kind[static_cast<std::size_t>(column_of(var))];
}

void HighsSolver::set_variable_kind(Variable var, VarKind kind) {
    std::int64_t col = column_of(var);
    if (lp_.has_quadratic_objective() && kind != VarKind::Continuous) throw_unsupported_miqp();
    Highs_changeColIntegrality(highs_, static_cast<HighsInt>(col), to_highs_integrality(kind));
    lp_.col_kind[static_cast<std::size_t>(col)] = kind;
}

void HighsSolver::set_progress_callback(ProgressCallback callback) {
    bool was_set = static_cast<bool>(progress_callback_);
    bool now_set = static_cast<bool>(callback);
    if (now_set != was_set) {
        for (HighsInt type : kProgressCallbackTypes) {
            if (now_set) {
                Highs_startCallback(highs_, type);
            } else {
                Highs_stopCallback(highs_, type);
            }
        }
    }
    progress_callback_ = std::move(callback);
}

void HighsSolver::set_sensitivity_ranging(bool enabled) { sensitivity_ranging_ = enabled; }

bool HighsSolver::invoke_progress_callback(const SolverProgress& progress) const {
    if (!progress_callback_) return true;
    return progress_callback_(progress);
}

namespace {

SolverRegistration highs_registration{"highs", [](const Model& model, const SolverOptions& options) {
                                           return std::make_unique<HighsSolver>(model, options);
                                       }};

}  // namespace

}  // namespace saiopt
