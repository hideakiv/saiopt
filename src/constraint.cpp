#include "saiopt/constraint.hpp"

namespace saiopt {

namespace {

// lhs - rhs folds every variable term onto one side; the resulting
// constant is the negation of what belongs on the right-hand side.
std::pair<LinearExpression, double> normalize(LinearExpression lhs, LinearExpression rhs) {
    LinearExpression diff = std::move(lhs) - std::move(rhs);
    double value = -diff.constant();
    diff -= diff.constant();
    return {std::move(diff), value};
}

}  // namespace

Constraint operator<=(LinearExpression lhs, LinearExpression rhs) {
    auto [expr, value] = normalize(std::move(lhs), std::move(rhs));
    return Constraint(std::move(expr), -kInfinity, value);
}

Constraint operator>=(LinearExpression lhs, LinearExpression rhs) {
    auto [expr, value] = normalize(std::move(lhs), std::move(rhs));
    return Constraint(std::move(expr), value, kInfinity);
}

Constraint operator==(LinearExpression lhs, LinearExpression rhs) {
    auto [expr, value] = normalize(std::move(lhs), std::move(rhs));
    return Constraint(std::move(expr), value, value);
}

LowerBoundedExpression operator<=(double lower, LinearExpression expr) {
    return LowerBoundedExpression(lower, std::move(expr));
}

Constraint operator<=(LowerBoundedExpression lhs, double upper) {
    double c = lhs.expr_.constant();
    return Constraint(lhs.expr_ - c, lhs.lower_ - c, upper - c);
}

UpperBoundedExpression operator>=(double upper, LinearExpression expr) {
    return UpperBoundedExpression(upper, std::move(expr));
}

Constraint operator>=(UpperBoundedExpression lhs, double lower) {
    double c = lhs.expr_.constant();
    return Constraint(lhs.expr_ - c, lower - c, lhs.upper_ - c);
}

}  // namespace saiopt
