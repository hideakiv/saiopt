#pragma once

#include <unordered_map>

#include "saiopt/variable.hpp"

namespace saiopt {

// A linear combination of variables plus a constant offset: sum(c_i * x_i) + b.
class LinearExpression {
public:
    LinearExpression() = default;
    LinearExpression(double constant) : constant_(constant) {}
    LinearExpression(Variable v) { terms_.emplace(v, 1.0); }

    LinearExpression& operator+=(const LinearExpression& rhs);
    LinearExpression& operator-=(const LinearExpression& rhs);
    LinearExpression& operator*=(double scalar);

    const std::unordered_map<Variable, double>& terms() const noexcept { return terms_; }
    double constant() const noexcept { return constant_; }

    // Coefficient of `var` in this expression, or 0.0 if it doesn't appear.
    double coefficient(Variable var) const {
        auto it = terms_.find(var);
        return it == terms_.end() ? 0.0 : it->second;
    }

    // Set the coefficient of `var`, adding the term if not already present
    // (or dropping it if `coeff` is 0.0).
    void set_coefficient(Variable var, double coeff) {
        if (coeff == 0.0) {
            terms_.erase(var);
        } else {
            terms_[var] = coeff;
        }
    }

private:
    std::unordered_map<Variable, double> terms_;
    double constant_ = 0.0;
};

LinearExpression operator+(LinearExpression lhs, const LinearExpression& rhs);
LinearExpression operator-(LinearExpression lhs, const LinearExpression& rhs);
LinearExpression operator-(LinearExpression expr);

LinearExpression operator*(double scalar, LinearExpression expr);
LinearExpression operator*(LinearExpression expr, double scalar);

}  // namespace saiopt
