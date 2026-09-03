#pragma once

#include <unordered_map>

#include "saiopt/variable.hpp"

namespace saiopt {

// A linear combination of variables plus a constant offset: sum(c_i * x_i) + b.
class LinearExpression {
public:
    LinearExpression() = default;
    LinearExpression(double constant) : constant_(constant) {}
    LinearExpression(Variable v) { terms_[v.index()] += 1.0; }

    LinearExpression& operator+=(const LinearExpression& rhs);
    LinearExpression& operator-=(const LinearExpression& rhs);
    LinearExpression& operator*=(double scalar);

    const std::unordered_map<std::size_t, double>& terms() const noexcept { return terms_; }
    double constant() const noexcept { return constant_; }

private:
    std::unordered_map<std::size_t, double> terms_;
    double constant_ = 0.0;
};

LinearExpression operator+(LinearExpression lhs, const LinearExpression& rhs);
LinearExpression operator-(LinearExpression lhs, const LinearExpression& rhs);
LinearExpression operator-(LinearExpression expr);

LinearExpression operator*(double scalar, LinearExpression expr);
LinearExpression operator*(LinearExpression expr, double scalar);

}  // namespace saiopt
