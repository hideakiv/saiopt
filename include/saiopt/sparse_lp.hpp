#pragma once

#include <cstdint>
#include <vector>

namespace saiopt {

class Model;

// Column-wise (CSC) sparse representation of a Model's constraint matrix,
// bounds, and objective -- the exact shape HiGHS' C API expects. Exposed
// directly so callers can inspect or hand-edit it before solving.
struct SparseLp {
    std::int64_t num_col = 0;
    std::int64_t num_row = 0;

    // CSC constraint matrix: a_start has num_col + 1 entries.
    std::vector<std::int64_t> a_start;
    std::vector<std::int64_t> a_index;
    std::vector<double> a_value;

    std::vector<double> col_cost;
    std::vector<double> col_lower;
    std::vector<double> col_upper;

    std::vector<double> row_lower;
    std::vector<double> row_upper;

    // HiGHS objective sense: +1 minimize, -1 maximize.
    int sense = 1;
    double offset = 0.0;
};

SparseLp flatten(const Model& model);

}  // namespace saiopt
