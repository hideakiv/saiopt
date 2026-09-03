#include "saiopt/constraint.hpp"

namespace saiopt {

namespace {

// lhs - rhs folds every variable term onto one side; the resulting
// constant is the negation of what belongs on the right-hand side.
Constraint make_constraint(LinearExpression lhs, ConstraintSense sense, LinearExpression rhs) {
    LinearExpression diff = std::move(lhs) - std::move(rhs);
    double rhs_value = -diff.constant();
    diff -= diff.constant();
    return Constraint(std::move(diff), sense, rhs_value);
}

}  // namespace

Constraint operator<=(LinearExpression lhs, LinearExpression rhs) {
    return make_constraint(std::move(lhs), ConstraintSense::Le, std::move(rhs));
}

Constraint operator>=(LinearExpression lhs, LinearExpression rhs) {
    return make_constraint(std::move(lhs), ConstraintSense::Ge, std::move(rhs));
}

Constraint operator==(LinearExpression lhs, LinearExpression rhs) {
    return make_constraint(std::move(lhs), ConstraintSense::Eq, std::move(rhs));
}

}  // namespace saiopt
