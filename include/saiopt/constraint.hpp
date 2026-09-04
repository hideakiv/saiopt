#pragma once

#include <utility>

#include "saiopt/expression.hpp"
#include "saiopt/infinity.hpp"

namespace saiopt {

// A normalized linear constraint: `lower <= expr <= upper`, where every
// variable term has been folded into `expr` and every constant into the
// bounds. Built via <=, >=, or == (each leaving the unused bound at
// +/-infinity), or directly as a two-sided range -- see
// LowerBoundedExpression/UpperBoundedExpression below.
class Constraint {
public:
    Constraint(LinearExpression expr, double lower, double upper)
        : expr_(std::move(expr)), lower_(lower), upper_(upper) {}

    // Coefficient of `var` on this constraint's normalized left-hand side,
    // or 0.0 if it doesn't appear.
    double coefficient(Variable var) const { return expr_.coefficient(var); }
    void set_coefficient(Variable var, double coeff) { expr_.set_coefficient(var, coeff); }

    const LinearExpression& expression() const noexcept { return expr_; }

    double lower() const noexcept { return lower_; }
    double upper() const noexcept { return upper_; }
    void set_lower(double value) { lower_ = value; }
    void set_upper(double value) { upper_ = value; }

private:
    LinearExpression expr_;
    double lower_;
    double upper_;
};

Constraint operator<=(LinearExpression lhs, LinearExpression rhs);
Constraint operator>=(LinearExpression lhs, LinearExpression rhs);
Constraint operator==(LinearExpression lhs, LinearExpression rhs);

// Produced by `lower <= expr`. Converts to a one-sided Constraint (lower <=
// expr <= +inf) on its own, or -- chained with `<= upper` -- builds a
// two-sided range constraint in one expression:
//
//   10.0 <= (x + y) <= 20.0
//
class LowerBoundedExpression {
public:
    LowerBoundedExpression(double lower, LinearExpression expr)
        : lower_(lower), expr_(std::move(expr)) {}

    operator Constraint() const {
        double c = expr_.constant();
        return Constraint(expr_ - c, lower_ - c, kInfinity);
    }

private:
    friend Constraint operator<=(LowerBoundedExpression lhs, double upper);

    double lower_;
    LinearExpression expr_;
};

// Symmetric to LowerBoundedExpression, for `upper >= expr >= lower`.
class UpperBoundedExpression {
public:
    UpperBoundedExpression(double upper, LinearExpression expr)
        : upper_(upper), expr_(std::move(expr)) {}

    operator Constraint() const {
        double c = expr_.constant();
        return Constraint(expr_ - c, -kInfinity, upper_ - c);
    }

private:
    friend Constraint operator>=(UpperBoundedExpression lhs, double lower);

    double upper_;
    LinearExpression expr_;
};

LowerBoundedExpression operator<=(double lower, LinearExpression expr);
Constraint operator<=(LowerBoundedExpression lhs, double upper);

UpperBoundedExpression operator>=(double upper, LinearExpression expr);
Constraint operator>=(UpperBoundedExpression lhs, double lower);

}  // namespace saiopt
