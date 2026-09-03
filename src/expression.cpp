#include "saiopt/expression.hpp"

namespace saiopt {

LinearExpression& LinearExpression::operator+=(const LinearExpression& rhs) {
    for (const auto& [index, coeff] : rhs.terms_) {
        terms_[index] += coeff;
    }
    constant_ += rhs.constant_;
    return *this;
}

LinearExpression& LinearExpression::operator-=(const LinearExpression& rhs) {
    for (const auto& [index, coeff] : rhs.terms_) {
        terms_[index] -= coeff;
    }
    constant_ -= rhs.constant_;
    return *this;
}

LinearExpression& LinearExpression::operator*=(double scalar) {
    for (auto& [index, coeff] : terms_) {
        coeff *= scalar;
    }
    constant_ *= scalar;
    return *this;
}

LinearExpression operator+(LinearExpression lhs, const LinearExpression& rhs) {
    lhs += rhs;
    return lhs;
}

LinearExpression operator-(LinearExpression lhs, const LinearExpression& rhs) {
    lhs -= rhs;
    return lhs;
}

LinearExpression operator-(LinearExpression expr) {
    expr *= -1.0;
    return expr;
}

LinearExpression operator*(double scalar, LinearExpression expr) {
    expr *= scalar;
    return expr;
}

LinearExpression operator*(LinearExpression expr, double scalar) {
    expr *= scalar;
    return expr;
}

}  // namespace saiopt
