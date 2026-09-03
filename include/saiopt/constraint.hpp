#pragma once

#include <utility>

#include "saiopt/expression.hpp"
#include "saiopt/infinity.hpp"

namespace saiopt {

enum class ConstraintSense { Le, Ge, Eq };

// A normalized linear constraint: `expr <sense> rhs`, where every variable
// term has been folded into `expr` and every constant into `rhs`.
class Constraint {
public:
    Constraint(LinearExpression expr, ConstraintSense sense, double rhs)
        : expr_(std::move(expr)), sense_(sense), rhs_(rhs) {}

    // Coefficient of `var` on this constraint's normalized left-hand side,
    // or 0.0 if it doesn't appear.
    double coefficient(Variable var) const { return expr_.coefficient(var); }
    void set_coefficient(Variable var, double coeff) { expr_.set_coefficient(var, coeff); }

    ConstraintSense sense() const noexcept { return sense_; }
    const LinearExpression& expression() const noexcept { return expr_; }

    double rhs() const noexcept { return rhs_; }
    void set_rhs(double value) { rhs_ = value; }

private:
    LinearExpression expr_;
    ConstraintSense sense_;
    double rhs_;
};

Constraint operator<=(LinearExpression lhs, LinearExpression rhs);
Constraint operator>=(LinearExpression lhs, LinearExpression rhs);
Constraint operator==(LinearExpression lhs, LinearExpression rhs);

}  // namespace saiopt
