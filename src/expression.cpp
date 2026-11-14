#include "saiopt/expression.hpp"

namespace saiopt {

LinearExpression& LinearExpression::operator+=(const LinearExpression& rhs) {
    for (const auto& [var, coeff] : rhs.terms_) {
        terms_[var] += coeff;
    }
    constant_ += rhs.constant_;
    return *this;
}

LinearExpression& LinearExpression::operator-=(const LinearExpression& rhs) {
    for (const auto& [var, coeff] : rhs.terms_) {
        terms_[var] -= coeff;
    }
    constant_ -= rhs.constant_;
    return *this;
}

LinearExpression& LinearExpression::operator*=(double scalar) {
    for (auto& entry : terms_) {
        entry.second *= scalar;
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

namespace {

bool variable_less(Variable a, Variable b) {
    if (a.block().raw() != b.block().raw()) return a.block().raw() < b.block().raw();
    return a.index() < b.index();
}

}  // namespace

VariablePair::VariablePair(Variable a, Variable b)
    : first_(variable_less(b, a) ? b : a), second_(variable_less(b, a) ? a : b) {}

QuadraticExpression& QuadraticExpression::operator+=(const QuadraticExpression& rhs) {
    linear_ += rhs.linear_;
    for (const auto& [pair, coeff] : rhs.quadratic_) {
        quadratic_[pair] += coeff;
    }
    return *this;
}

QuadraticExpression& QuadraticExpression::operator-=(const QuadraticExpression& rhs) {
    linear_ -= rhs.linear_;
    for (const auto& [pair, coeff] : rhs.quadratic_) {
        quadratic_[pair] -= coeff;
    }
    return *this;
}

QuadraticExpression& QuadraticExpression::operator*=(double scalar) {
    linear_ *= scalar;
    for (auto& entry : quadratic_) {
        entry.second *= scalar;
    }
    return *this;
}

void QuadraticExpression::set_coefficient(Variable a, Variable b, double coeff) {
    VariablePair pair(a, b);
    if (coeff == 0.0) {
        quadratic_.erase(pair);
    } else {
        quadratic_.insert_or_assign(pair, coeff);
    }
}

QuadraticExpression operator*(const LinearExpression& lhs, const LinearExpression& rhs) {
    // (sum a_i x_i + a0) * (sum b_j x_j + b0)
    //   = sum a_i b_j x_i x_j + b0 * sum a_i x_i + a0 * sum b_j x_j + a0 b0
    LinearExpression linear_lhs = lhs;
    linear_lhs *= rhs.constant();
    LinearExpression linear_rhs = rhs;
    linear_rhs *= lhs.constant();
    LinearExpression linear = linear_lhs + linear_rhs;
    // Both scaled copies carried a0 * b0 as their constant; keep just one.
    linear -= LinearExpression(lhs.constant() * rhs.constant());

    QuadraticExpression product(std::move(linear));
    for (const auto& [x, a] : lhs.terms()) {
        for (const auto& [y, b] : rhs.terms()) {
            product.set_coefficient(x, y, product.coefficient(x, y) + a * b);
        }
    }
    return product;
}

}  // namespace saiopt
