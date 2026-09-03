#include "saiopt/highs_solver.hpp"

#include <interfaces/highs_c_api.h>

#include <vector>

#include "saiopt/sparse_lp.hpp"

namespace saiopt {

namespace {

std::vector<HighsInt> to_highs_int(const std::vector<std::int64_t>& values) {
    std::vector<HighsInt> result(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        result[i] = static_cast<HighsInt>(values[i]);
    }
    return result;
}

}  // namespace

SolveResult HighsSolver::solve(const Model& model) const {
    SparseLp lp = flatten(model);

    void* highs = Highs_create();
    Highs_setBoolOptionValue(highs, "output_flag", 0);

    std::vector<HighsInt> a_start = to_highs_int(lp.a_start);
    std::vector<HighsInt> a_index = to_highs_int(lp.a_index);

    HighsInt status = Highs_passLp(
        highs, static_cast<HighsInt>(lp.num_col), static_cast<HighsInt>(lp.num_row),
        static_cast<HighsInt>(lp.a_value.size()), kHighsMatrixFormatColwise, lp.sense, lp.offset,
        lp.col_cost.data(), lp.col_lower.data(), lp.col_upper.data(), lp.row_lower.data(),
        lp.row_upper.data(), a_start.data(), a_index.data(), lp.a_value.data());

    for (std::size_t i = 0; status == 0 && i < lp.col_kind.size(); ++i) {
        if (lp.col_kind[i] != VarKind::Continuous) {
            Highs_changeColIntegrality(highs, static_cast<HighsInt>(i), kHighsVarTypeInteger);
        }
    }

    SolveResult result;
    if (status == 0) {
        Highs_run(highs);

        HighsInt model_status = Highs_getModelStatus(highs);
        result.optimal = (model_status == kHighsModelStatusOptimal);

        std::vector<double> col_value(lp.num_col);
        std::vector<double> row_value(lp.num_row);
        std::vector<double> col_dual(lp.num_col);
        std::vector<double> row_dual(lp.num_row);
        Highs_getSolution(highs, col_value.data(), col_dual.data(), row_value.data(),
                           row_dual.data());

        for (std::size_t i = 0; i < lp.col_variable.size(); ++i) {
            result.variable_values.emplace(lp.col_variable[i], col_value[i]);
        }
        result.objective_value = Highs_getObjectiveValue(highs);
    } else {
        result.status_message = "HiGHS rejected the model (Highs_passLp failed)";
    }

    Highs_destroy(highs);
    return result;
}

}  // namespace saiopt
