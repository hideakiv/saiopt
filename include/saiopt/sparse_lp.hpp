#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "saiopt/constraint_ref.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

class Model;

// Column-wise (CSC) sparse representation of a Model's flattened Block
// tree -- a common shape for LP/MILP solver APIs (e.g. HiGHS' C API) to
// consume directly. Exposed so a Solver backend, or a caller, can inspect
// or hand-edit it before solving.
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
    std::vector<VarKind> col_kind;

    // The Variable each column originated from -- used to map a solution
    // back onto the original Block tree.
    std::vector<Variable> col_variable;

    // Each column's name, qualified by the chain of child-block names
    // leading down to it (e.g. "region[3].x[(a, b)]").
    std::vector<std::string> col_name;

    std::vector<double> row_lower;
    std::vector<double> row_upper;

    // The ConstraintRef each row originated from -- used to map dual
    // values and bound ranging back onto the original Block tree.
    std::vector<ConstraintRef> row_constraint;

    // Each row's name, qualified the same way as col_name.
    std::vector<std::string> row_name;

    // HiGHS objective sense: +1 minimize, -1 maximize.
    int sense = 1;
    double offset = 0.0;
};

SparseLp flatten(const Model& model);

// `names` (e.g. a SparseLp's col_name or row_name) rewritten so each is
// a legal, distinct identifier in LP- and MPS-format model files, which
// forbid whitespace, brackets, and most operator characters: '[' and
// ']' become '(' and ')', whitespace is dropped, any other character
// outside letters, digits, and "_.(),~" becomes '_', and a name that
// would start with a digit or '.' gets a leading '_'. A name that
// collides with an earlier one after rewriting gets a "~2", "~3", ...
// suffix. Backends pass these to their solver so a written model file
// (see Solver::write_model) is readable back in.
std::vector<std::string> file_safe_names(const std::vector<std::string>& names);

}  // namespace saiopt
