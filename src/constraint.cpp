#include "saiopt/constraint.hpp"

namespace saiopt {

namespace {
// Moves an expression's constant term into the bound, leaving only the
// variable terms behind -- Constraint stores terms and bounds separately.
double extract_constant(LinearExpression& expr) {
    double constant = expr.constant();
    expr -= constant;
    return constant;
}
}  // namespace

Constraint operator<=(LinearExpression lhs, double rhs) {
    double bound = rhs - extract_constant(lhs);
    return Constraint(std::move(lhs), -kInfinity, bound);
}

Constraint operator>=(LinearExpression lhs, double rhs) {
    double bound = rhs - extract_constant(lhs);
    return Constraint(std::move(lhs), bound, kInfinity);
}

Constraint operator==(LinearExpression lhs, double rhs) {
    double bound = rhs - extract_constant(lhs);
    return Constraint(std::move(lhs), bound, bound);
}

Constraint operator<=(double lhs, LinearExpression rhs) {
    return std::move(rhs) >= lhs;
}

Constraint operator>=(double lhs, LinearExpression rhs) {
    return std::move(rhs) <= lhs;
}

}  // namespace saiopt
