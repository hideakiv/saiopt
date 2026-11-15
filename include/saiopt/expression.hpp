#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

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

// An unordered pair of variables, the key of one QuadraticExpression
// term: {x, y} and {y, x} are the same pair (the constructor puts them in
// a canonical order), and {x, x} is x's square.
class VariablePair {
public:
    VariablePair(Variable a, Variable b);

    Variable first() const noexcept { return first_; }
    Variable second() const noexcept { return second_; }

    friend bool operator==(const VariablePair& a, const VariablePair& b) noexcept {
        return a.first_.is_same(b.first_) && a.second_.is_same(b.second_);
    }

private:
    Variable first_;
    Variable second_;
};

}  // namespace saiopt

template <>
struct std::hash<saiopt::VariablePair> {
    std::size_t operator()(const saiopt::VariablePair& p) const noexcept {
        return saiopt::detail::hash_pair(std::hash<saiopt::Variable>{}(p.first()),
                                         std::hash<saiopt::Variable>{}(p.second()));
    }
};

namespace saiopt {

// A quadratic function of variables: sum(q_ij * x_i * x_j) plus a
// LinearExpression, with one term per unordered pair {x_i, x_j} -- x*y and
// y*x accumulate into the same term, and x*x is x's square. Built by
// multiplying two LinearExpressions (e.g. x * y, (x + 1) * (y - 2)), and
// usable only as an objective: constraints stay linear, so `x * y <= 3`
// doesn't compile.
//
// The arithmetic operators are hidden friends, found only when an operand
// is already a QuadraticExpression -- otherwise `x + y` on two Variables
// would be ambiguous between this and LinearExpression's own operator+,
// since a Variable converts to either.
class QuadraticExpression {
public:
    QuadraticExpression() = default;
    QuadraticExpression(double constant) : linear_(constant) {}
    QuadraticExpression(Variable v) : linear_(v) {}
    QuadraticExpression(LinearExpression linear) : linear_(std::move(linear)) {}

    QuadraticExpression& operator+=(const QuadraticExpression& rhs);
    QuadraticExpression& operator-=(const QuadraticExpression& rhs);
    QuadraticExpression& operator*=(double scalar);

    const LinearExpression& linear() const noexcept { return linear_; }
    const std::unordered_map<VariablePair, double>& quadratic_terms() const noexcept {
        return quadratic_;
    }
    double constant() const noexcept { return linear_.constant(); }

    // Coefficient of the {a, b} term (a's square when a == b), or 0.0 if
    // it doesn't appear.
    double coefficient(Variable a, Variable b) const {
        auto it = quadratic_.find(VariablePair(a, b));
        return it == quadratic_.end() ? 0.0 : it->second;
    }

    // Set the coefficient of the {a, b} term, adding it if not already
    // present (or dropping it if `coeff` is 0.0).
    void set_coefficient(Variable a, Variable b, double coeff);

    friend QuadraticExpression operator+(QuadraticExpression lhs, const QuadraticExpression& rhs) {
        lhs += rhs;
        return lhs;
    }
    friend QuadraticExpression operator-(QuadraticExpression lhs, const QuadraticExpression& rhs) {
        lhs -= rhs;
        return lhs;
    }
    friend QuadraticExpression operator-(QuadraticExpression expr) {
        expr *= -1.0;
        return expr;
    }
    friend QuadraticExpression operator*(double scalar, QuadraticExpression expr) {
        expr *= scalar;
        return expr;
    }
    friend QuadraticExpression operator*(QuadraticExpression expr, double scalar) {
        expr *= scalar;
        return expr;
    }

private:
    LinearExpression linear_;
    std::unordered_map<VariablePair, double> quadratic_;
};

// The product of two linear expressions, e.g. x * y or (x + 1) * (y - 2).
QuadraticExpression operator*(const LinearExpression& lhs, const LinearExpression& rhs);

}  // namespace saiopt
