#pragma once

#include <string>
#include <utility>

#include "saiopt/expression.hpp"

namespace saiopt {

// HiGHS convention for an unbounded row/column side.
inline constexpr double kInfinity = 1.0e30;

// A linear expression restricted to lie within [lower, upper].
class Constraint {
public:
    Constraint(LinearExpression expr, double lower, double upper)
        : expr_(std::move(expr)), lower_(lower), upper_(upper) {}

    const LinearExpression& expression() const noexcept { return expr_; }
    double lower() const noexcept { return lower_; }
    double upper() const noexcept { return upper_; }

private:
    LinearExpression expr_;
    double lower_;
    double upper_;
};

Constraint operator<=(LinearExpression lhs, double rhs);
Constraint operator>=(LinearExpression lhs, double rhs);
Constraint operator==(LinearExpression lhs, double rhs);

Constraint operator<=(double lhs, LinearExpression rhs);
Constraint operator>=(double lhs, LinearExpression rhs);

}  // namespace saiopt
