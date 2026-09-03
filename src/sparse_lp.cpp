#include "saiopt/sparse_lp.hpp"

#include <map>

#include "saiopt/model.hpp"

namespace saiopt {

SparseLp flatten(const Model& model) {
    SparseLp lp;
    lp.num_col = static_cast<std::int64_t>(model.num_variables());
    lp.num_row = static_cast<std::int64_t>(model.num_constraints());

    lp.col_cost.assign(model.num_variables(), 0.0);
    for (const auto& [index, coeff] : model.objective().expression.terms()) {
        lp.col_cost[index] = coeff;
    }
    lp.offset = model.objective().expression.constant();
    lp.sense = model.objective().sense == ObjectiveSense::Maximize ? -1 : 1;

    lp.col_lower.reserve(model.num_variables());
    lp.col_upper.reserve(model.num_variables());
    for (std::size_t i = 0; i < model.num_variables(); ++i) {
        lp.col_lower.push_back(model.variable_lower(i));
        lp.col_upper.push_back(model.variable_upper(i));
    }

    lp.row_lower.reserve(model.num_constraints());
    lp.row_upper.reserve(model.num_constraints());
    for (std::size_t r = 0; r < model.num_constraints(); ++r) {
        lp.row_lower.push_back(model.constraint(r).lower());
        lp.row_upper.push_back(model.constraint(r).upper());
    }

    // Build the constraint matrix column-by-column (CSC): for each variable,
    // collect (row, coefficient) pairs from every constraint it appears in.
    std::vector<std::map<std::int64_t, double>> columns(model.num_variables());
    for (std::size_t r = 0; r < model.num_constraints(); ++r) {
        for (const auto& [index, coeff] : model.constraint(r).expression().terms()) {
            if (coeff != 0.0) {
                columns[index][static_cast<std::int64_t>(r)] += coeff;
            }
        }
    }

    lp.a_start.reserve(model.num_variables() + 1);
    lp.a_start.push_back(0);
    for (const auto& column : columns) {
        for (const auto& [row, coeff] : column) {
            lp.a_index.push_back(row);
            lp.a_value.push_back(coeff);
        }
        lp.a_start.push_back(static_cast<std::int64_t>(lp.a_index.size()));
    }

    return lp;
}

}  // namespace saiopt
