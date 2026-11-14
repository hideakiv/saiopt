#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>

#include <cmath>
#include <sstream>
#include <string>

#include "bindings.hpp"
#include "saiopt/infinity.hpp"

namespace saiopt::python {

namespace {

bool is_expression(nb::handle h) {
    return nb::isinstance<Variable>(h) || nb::isinstance<LinearExpression>(h) ||
           nb::isinstance<QuadraticExpression>(h) || nb::isinstance<PyConstraint>(h);
}

// Any real Python number (int, float, bool, numpy scalars, ...) --
// anything with __float__ or __index__ that isn't one of saiopt's own
// expression types or a complex.
bool is_number(nb::handle h) {
    if (is_expression(h)) return false;
    return PyNumber_Check(h.ptr()) && !PyComplex_Check(h.ptr());
}

double to_double(nb::handle h) {
    double value = PyFloat_AsDouble(h.ptr());
    if (value == -1.0 && PyErr_Occurred()) throw nb::python_error();
    return value;
}

nb::object not_implemented() { return nb::borrow(Py_NotImplemented); }

bool is_quadratic(nb::handle h) { return nb::isinstance<QuadraticExpression>(h); }

nb::object add(nb::handle a, nb::handle b) {
    if (auto la = as_linear(a)) {
        if (auto lb = as_linear(b)) return nb::cast(*la + *lb);
    }
    if (auto qa = as_quadratic(a)) {
        if (auto qb = as_quadratic(b)) return nb::cast(*qa + *qb);
    }
    return not_implemented();
}

nb::object sub(nb::handle a, nb::handle b) {
    if (auto la = as_linear(a)) {
        if (auto lb = as_linear(b)) return nb::cast(*la - *lb);
    }
    if (auto qa = as_quadratic(a)) {
        if (auto qb = as_quadratic(b)) return nb::cast(*qa - *qb);
    }
    return not_implemented();
}

// `expr * scalar`, keeping expr's degree.
nb::object scale(nb::handle expr, double scalar) {
    if (auto l = as_linear(expr)) return nb::cast(*l * scalar);
    if (auto q = as_quadratic(expr)) return nb::cast(*q * scalar);
    return not_implemented();
}

nb::object mul(nb::handle a, nb::handle b) {
    if (is_number(b)) return scale(a, to_double(b));
    if (is_number(a)) return scale(b, to_double(a));
    auto la = as_linear(a);
    auto lb = as_linear(b);
    if (la && lb) return nb::cast(*la * *lb);
    if ((la || is_quadratic(a)) && (lb || is_quadratic(b))) {
        throw nb::type_error("this product would be of degree 3 or more; only up to quadratic is supported");
    }
    return not_implemented();
}

nb::object truediv(nb::handle a, nb::handle b) {
    if (!is_number(b)) {
        if (is_expression(b)) throw nb::type_error("can only divide an expression by a number");
        return not_implemented();
    }
    double divisor = to_double(b);
    if (divisor == 0.0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
        throw nb::python_error();
    }
    return scale(a, 1.0 / divisor);
}

nb::object power(nb::handle base, nb::handle exponent) {
    if (!PyLong_Check(exponent.ptr())) throw nb::type_error("an expression can only be raised to the power 1 or 2");
    long e = PyLong_AsLong(exponent.ptr());
    if (e == 1) return add(base, nb::float_(0.0));
    if (e == 2) return mul(base, base);
    throw nb::type_error("an expression can only be raised to the power 1 or 2");
}

enum class Comparison { LessEqual, GreaterEqual, Equal };

nb::object compare(nb::handle a, nb::handle b, Comparison op) {
    auto la = as_linear(a);
    auto lb = as_linear(b);
    if (!la || !lb) {
        if ((la || is_quadratic(a)) && (lb || is_quadratic(b))) {
            throw nb::type_error("constraints must be linear; a quadratic expression can only be an objective");
        }
        return not_implemented();
    }
    PyConstraint result{op == Comparison::LessEqual      ? (*la <= *lb)
                        : op == Comparison::GreaterEqual ? (*la >= *lb)
                                                         : (*la == *lb),
                        std::nullopt};
    if (op == Comparison::Equal && nb::isinstance<Variable>(a) && nb::isinstance<Variable>(b)) {
        result.variable_equality.emplace(nb::cast<Variable>(a), nb::cast<Variable>(b));
    }
    return nb::cast(std::move(result));
}

// The arithmetic and comparison operators every expression type shares.
// `self` is taken as a plain handle: these only ever run on an instance
// of the class they're bound on, and the shared helpers above already
// dispatch on the operand types themselves.
template <typename T>
void bind_operators(nb::class_<T>& cls) {
    cls.def("__add__", [](nb::handle self, nb::handle other) { return add(self, other); }, nb::is_operator())
        .def("__radd__", [](nb::handle self, nb::handle other) { return add(other, self); }, nb::is_operator())
        .def("__sub__", [](nb::handle self, nb::handle other) { return sub(self, other); }, nb::is_operator())
        .def("__rsub__", [](nb::handle self, nb::handle other) { return sub(other, self); }, nb::is_operator())
        .def("__mul__", [](nb::handle self, nb::handle other) { return mul(self, other); }, nb::is_operator())
        .def("__rmul__", [](nb::handle self, nb::handle other) { return mul(other, self); }, nb::is_operator())
        .def("__truediv__", [](nb::handle self, nb::handle other) { return truediv(self, other); },
             nb::is_operator())
        .def("__pow__", [](nb::handle self, nb::handle exponent) { return power(self, exponent); },
             nb::is_operator())
        .def("__neg__", [](nb::handle self) { return scale(self, -1.0); }, nb::is_operator())
        .def("__pos__", [](nb::handle self) { return add(self, nb::float_(0.0)); }, nb::is_operator())
        .def("__le__", [](nb::handle self, nb::handle other) { return compare(self, other, Comparison::LessEqual); },
             nb::is_operator())
        .def("__ge__",
             [](nb::handle self, nb::handle other) { return compare(self, other, Comparison::GreaterEqual); },
             nb::is_operator())
        .def("__eq__", [](nb::handle self, nb::handle other) { return compare(self, other, Comparison::Equal); },
             nb::is_operator());
}

std::string format_number(double value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

}  // namespace

std::optional<LinearExpression> as_linear(nb::handle h) {
    if (nb::isinstance<LinearExpression>(h)) return nb::cast<const LinearExpression&>(h);
    if (nb::isinstance<Variable>(h)) return LinearExpression(nb::cast<Variable>(h));
    if (is_number(h)) return LinearExpression(to_double(h));
    return std::nullopt;
}

std::optional<QuadraticExpression> as_quadratic(nb::handle h) {
    if (nb::isinstance<QuadraticExpression>(h)) return nb::cast<const QuadraticExpression&>(h);
    if (auto l = as_linear(h)) return QuadraticExpression(std::move(*l));
    return std::nullopt;
}

double to_bound(double value) {
    if (value >= kInfinity) return kInfinity;
    if (value <= -kInfinity) return -kInfinity;
    return value;
}

void bind_expressions(nb::module_& m) {
    m.attr("inf") = kInfinity;

    nb::enum_<VarKind>(m, "VarKind", "A variable's kind.")
        .value("Continuous", VarKind::Continuous)
        .value("Integer", VarKind::Integer)
        .value("Binary", VarKind::Binary);

    nb::class_<Variable> variable(m, "Variable",
                                  "A handle to a variable owned by a Block or Model. Created by add_var/add_vars "
                                  "only; compare two with `is_same`, since `==` builds a constraint.");
    variable
        .def("is_same", [](const Variable& a, const Variable& b) { return a == b; }, "other"_a,
             "Whether `other` is the same variable.")
        .def("__hash__", [](const Variable& v) { return std::hash<Variable>{}(v); })
        .def(
            "__ne__",
            [](nb::handle self, nb::handle other) -> nb::object {
                if (!nb::isinstance<Variable>(other)) return not_implemented();
                return nb::bool_(!(nb::cast<Variable>(self) == nb::cast<Variable>(other)));
            },
            nb::is_operator())
        .def("__repr__", [](const Variable& v) {
            return "Variable(block=" + std::to_string(v.block().raw()) + ", index=" + std::to_string(v.index()) + ")";
        });
    bind_operators(variable);

    nb::class_<LinearExpression> linear(m, "LinearExpression",
                                        "A linear combination of variables plus a constant.");
    linear.def(nb::init<>())
        .def(nb::init<double>(), "constant"_a)
        .def_prop_ro("terms", &LinearExpression::terms, "Each variable's coefficient, as a new dict.")
        .def_prop_ro("constant", &LinearExpression::constant)
        .def("coefficient", &LinearExpression::coefficient, "var"_a)
        .def("set_coefficient", &LinearExpression::set_coefficient, "var"_a, "coeff"_a)
        .def(
            "__iadd__",
            [](nb::handle self, nb::handle other) -> nb::object {
                if (auto l = as_linear(other)) {
                    nb::cast<LinearExpression&>(self) += *l;
                    return nb::borrow(self);
                }
                return add(self, other);
            },
            nb::is_operator())
        .def(
            "__isub__",
            [](nb::handle self, nb::handle other) -> nb::object {
                if (auto l = as_linear(other)) {
                    nb::cast<LinearExpression&>(self) -= *l;
                    return nb::borrow(self);
                }
                return sub(self, other);
            },
            nb::is_operator())
        .def("__repr__", [](const LinearExpression& e) {
            return "LinearExpression(" + std::to_string(e.terms().size()) +
                   " terms, constant=" + format_number(e.constant()) + ")";
        });
    linear.attr("__hash__") = nb::none();
    bind_operators(linear);

    nb::class_<QuadraticExpression> quadratic(
        m, "QuadraticExpression",
        "A quadratic function of variables: one coefficient per unordered pair of variables, plus a "
        "LinearExpression. Usable only as an objective.");
    quadratic.def(nb::init<>())
        .def_prop_ro("linear", &QuadraticExpression::linear)
        .def_prop_ro(
            "quadratic_terms",
            [](const QuadraticExpression& q) {
                nb::dict terms;
                for (const auto& [pair, coeff] : q.quadratic_terms()) {
                    terms[nb::make_tuple(pair.first(), pair.second())] = coeff;
                }
                return terms;
            },
            "Each {a, b} term's coefficient, keyed by an (a, b) tuple, as a new dict.")
        .def_prop_ro("constant", &QuadraticExpression::constant)
        .def("coefficient", &QuadraticExpression::coefficient, "a"_a, "b"_a)
        .def("set_coefficient", &QuadraticExpression::set_coefficient, "a"_a, "b"_a, "coeff"_a)
        .def(
            "__iadd__",
            [](nb::handle self, nb::handle other) -> nb::object {
                if (auto q = as_quadratic(other)) {
                    nb::cast<QuadraticExpression&>(self) += *q;
                    return nb::borrow(self);
                }
                return not_implemented();
            },
            nb::is_operator())
        .def(
            "__isub__",
            [](nb::handle self, nb::handle other) -> nb::object {
                if (auto q = as_quadratic(other)) {
                    nb::cast<QuadraticExpression&>(self) -= *q;
                    return nb::borrow(self);
                }
                return not_implemented();
            },
            nb::is_operator())
        .def("__repr__", [](const QuadraticExpression& q) {
            return "QuadraticExpression(" + std::to_string(q.quadratic_terms().size()) + " quadratic terms, " +
                   std::to_string(q.linear().terms().size()) + " linear terms, constant=" +
                   format_number(q.constant()) + ")";
        });
    quadratic.attr("__hash__") = nb::none();
    bind_operators(quadratic);

    nb::class_<PyConstraint>(m, "Constraint",
                             "A linear constraint `lower <= expression <= upper`. Built by <=, >=, or == on "
                             "expressions, or directly as Constraint(expr, lower, upper) for a two-sided range "
                             "(Python can't chain `lower <= expr <= upper` into one).")
        .def(
            "__init__",
            [](PyConstraint* self, nb::handle expr, double lower, double upper) {
                auto l = as_linear(expr);
                if (!l) throw nb::type_error("a Constraint's expression must be linear");
                new (self) PyConstraint{Constraint(std::move(*l), to_bound(lower), to_bound(upper)), std::nullopt};
            },
            "expression"_a, "lower"_a = -kInfinity, "upper"_a = kInfinity)
        .def_prop_ro("expression", [](const PyConstraint& c) { return c.constraint.expression(); })
        .def_prop_ro("lower", [](const PyConstraint& c) { return c.constraint.lower(); })
        .def_prop_ro("upper", [](const PyConstraint& c) { return c.constraint.upper(); })
        .def("coefficient", [](const PyConstraint& c, Variable var) { return c.constraint.coefficient(var); },
             "var"_a)
        .def("__bool__",
             [](const PyConstraint& c) {
                 if (c.variable_equality) return c.variable_equality->first == c.variable_equality->second;
                 throw nb::type_error(
                     "a Constraint has no truth value -- for a two-sided range, write "
                     "Constraint(expr, lower, upper) instead of chaining `lower <= expr <= upper`");
             })
        .def("__repr__", [](const PyConstraint& c) {
            return "Constraint(" + format_number(c.constraint.lower()) + " <= " +
                   std::to_string(c.constraint.expression().terms().size()) +
                   " terms <= " + format_number(c.constraint.upper()) + ")";
        });

    m.def(
        "quicksum",
        [](nb::iterable items) -> nb::object {
            LinearExpression linear_total;
            std::optional<QuadraticExpression> quadratic_total;
            for (nb::handle item : items) {
                if (!quadratic_total) {
                    if (auto l = as_linear(item)) {
                        linear_total += *l;
                        continue;
                    }
                    quadratic_total.emplace(std::move(linear_total));
                }
                auto q = as_quadratic(item);
                if (!q) throw nb::type_error("quicksum() can only add numbers and expressions");
                *quadratic_total += *q;
            }
            if (quadratic_total) return nb::cast(std::move(*quadratic_total));
            return nb::cast(std::move(linear_total));
        },
        "items"_a,
        "Sum numbers and expressions in place -- unlike the built-in sum(), which copies the running "
        "total on every addition, so it takes quadratic time over a long sequence.");
}

}  // namespace saiopt::python
