#include "saiopt/solvers/highs_solver.hpp"

#include <interfaces/highs_c_api.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>
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

// HiGHS has no separate "binary" integrality -- a binary variable is just
// an integer one with [0, 1] bounds, so saiopt's own VarKind (which does
// distinguish them, for lp_.col_kind and is_pure_lp) collapses to this.
HighsInt to_highs_integrality(VarKind kind) {
    return kind == VarKind::Continuous ? kHighsVarTypeContinuous : kHighsVarTypeInteger;
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
void start_progress_callback(void* highs, HighsSolver* self) {
    HighsInt status = Highs_setCallback(highs, &highs_progress_trampoline, self);
    if (status != kHighsStatusOk) {
        throw std::runtime_error("HiGHS rejected the progress callback registration");
    }
    Highs_startCallback(highs, kHighsCallbackSimplexInterrupt);
    Highs_startCallback(highs, kHighsCallbackIpmInterrupt);
    Highs_startCallback(highs, kHighsCallbackMipInterrupt);
}

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

}  // namespace

HighsSolver::HighsSolver(const Model& model, const SolverOptions& options) : lp_(flatten(model)) {
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

        for (std::size_t i = 0; i < lp_.col_kind.size(); ++i) {
            if (lp_.col_kind[i] != VarKind::Continuous) {
                Highs_changeColIntegrality(highs_, static_cast<HighsInt>(i), kHighsVarTypeInteger);
            }
        }

        start_progress_callback(highs_, this);
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
      progress_callback_(std::move(other.progress_callback_)) {
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
    result.optimal = (model_status == kHighsModelStatusOptimal);

    std::vector<double> col_value(lp_.num_col);
    std::vector<double> row_value(lp_.num_row);
    std::vector<double> col_dual(lp_.num_col);
    std::vector<double> row_dual(lp_.num_row);
    Highs_getSolution(highs_, col_value.data(), col_dual.data(), row_value.data(), row_dual.data());

    for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
        result.variable_values.emplace(lp_.col_variable[i], col_value[i]);
    }
    result.objective_value = Highs_getObjectiveValue(highs_);

    // HiGHS only produces meaningful duals/reduced costs/ranging for an
    // LP basis: for a model with any integer/binary column, the dual
    // arrays from Highs_getSolution come back all zero (not the final
    // LP relaxation's values) and Highs_getRanging fails outright, so
    // leave these unset rather than publish misleading zeros.
    if (result.optimal && is_pure_lp(lp_)) {
        for (std::size_t i = 0; i < lp_.col_variable.size(); ++i) {
            result.reduced_costs.emplace(lp_.col_variable[i], col_dual[i]);
        }
        for (std::size_t i = 0; i < lp_.row_constraint.size(); ++i) {
            result.duals.emplace(lp_.row_constraint[i], row_dual[i]);
        }
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

    return result;
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
    Highs_changeColIntegrality(highs_, static_cast<HighsInt>(col), to_highs_integrality(kind));
    lp_.col_kind[static_cast<std::size_t>(col)] = kind;
}

void HighsSolver::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

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
