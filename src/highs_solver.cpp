#include "saiopt/highs_solver.hpp"

#include <interfaces/highs_c_api.h>

#include <algorithm>
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

}  // namespace

HighsSolver::HighsSolver(const Model& model) : lp_(flatten(model)) {
    highs_ = Highs_create();
    Highs_setBoolOptionValue(highs_, "output_flag", 0);

    std::vector<HighsInt> a_start = to_highs_int(lp_.a_start);
    std::vector<HighsInt> a_index = to_highs_int(lp_.a_index);

    HighsInt status = Highs_passLp(
        highs_, static_cast<HighsInt>(lp_.num_col), static_cast<HighsInt>(lp_.num_row),
        static_cast<HighsInt>(lp_.a_value.size()), kHighsMatrixFormatColwise, lp_.sense, lp_.offset,
        lp_.col_cost.data(), lp_.col_lower.data(), lp_.col_upper.data(), lp_.row_lower.data(),
        lp_.row_upper.data(), a_start.data(), a_index.data(), lp_.a_value.data());

    if (status != 0) {
        Highs_destroy(highs_);
        highs_ = nullptr;
        throw std::runtime_error("HiGHS rejected the model (Highs_passLp failed)");
    }

    for (std::size_t i = 0; i < lp_.col_kind.size(); ++i) {
        if (lp_.col_kind[i] != VarKind::Continuous) {
            Highs_changeColIntegrality(highs_, static_cast<HighsInt>(i), kHighsVarTypeInteger);
        }
    }
}

HighsSolver::HighsSolver(HighsSolver&& other) noexcept
    : highs_(other.highs_), lp_(std::move(other.lp_)) {
    other.highs_ = nullptr;
}

HighsSolver& HighsSolver::operator=(HighsSolver&& other) noexcept {
    if (this != &other) {
        if (highs_) Highs_destroy(highs_);
        highs_ = other.highs_;
        lp_ = std::move(other.lp_);
        other.highs_ = nullptr;
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

namespace {

SolverRegistration highs_registration{
    "highs", [](const Model& model) { return std::make_unique<HighsSolver>(model); }};

}  // namespace

}  // namespace saiopt
