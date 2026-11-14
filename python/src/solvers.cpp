#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>

#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "bindings.hpp"
#include "saiopt/solver.hpp"

namespace saiopt::python {

namespace {

// A Solver plus the slot a Python progress callback's exception is
// parked in: the callback runs inside the backend's solve loop, where a
// C++ exception can't safely unwind, so it's caught there, turned into
// an interrupt request, and re-raised once solve() returns.
struct PySolver {
    std::unique_ptr<Solver> solver;
    std::shared_ptr<std::exception_ptr> callback_error = std::make_shared<std::exception_ptr>();
};

SolverOptions to_options(const std::optional<nb::dict>& options) {
    SolverOptions result;
    if (!options) return result;
    for (auto [key, value] : *options) {
        std::string name = nb::cast<std::string>(key);
        // bool before int: a Python bool is also an int.
        if (PyBool_Check(value.ptr())) {
            result.set_bool(name, value.ptr() == Py_True);
        } else if (PyLong_Check(value.ptr())) {
            result.set_int(name, nb::cast<std::int64_t>(value));
        } else if (PyFloat_Check(value.ptr())) {
            result.set_double(name, nb::cast<double>(value));
        } else if (PyUnicode_Check(value.ptr())) {
            result.set_string(name, nb::cast<std::string>(value));
        } else {
            throw nb::type_error(("solver option \"" + name + "\" must be a bool, int, float, or str").c_str());
        }
    }
    return result;
}

// map.at(key), raising KeyError (not IndexError) when it's missing.
template <typename Map, typename Key>
auto lookup(const Map& map, const Key& key, const char* what) {
    auto it = map.find(key);
    if (it == map.end()) throw nb::key_error(what);
    return it->second;
}

}  // namespace

void bind_solvers(nb::module_& m) {
    nb::enum_<SolveStatus>(m, "SolveStatus", "How a solve() ended.")
        .value("Optimal", SolveStatus::Optimal)
        .value("Infeasible", SolveStatus::Infeasible)
        .value("Unbounded", SolveStatus::Unbounded)
        .value("InfeasibleOrUnbounded", SolveStatus::InfeasibleOrUnbounded)
        .value("TimeLimit", SolveStatus::TimeLimit)
        .value("IterationLimit", SolveStatus::IterationLimit)
        .value("NodeLimit", SolveStatus::NodeLimit)
        .value("SolutionLimit", SolveStatus::SolutionLimit)
        .value("GapLimit", SolveStatus::GapLimit)
        .value("ObjectiveLimit", SolveStatus::ObjectiveLimit)
        .value("MemoryLimit", SolveStatus::MemoryLimit)
        .value("Interrupted", SolveStatus::Interrupted)
        .value("Error", SolveStatus::Error)
        .value("Unknown", SolveStatus::Unknown);

    nb::class_<SensitivityRange>(m, "SensitivityRange")
        .def_ro("lower", &SensitivityRange::lower)
        .def_ro("upper", &SensitivityRange::upper)
        .def("__repr__", [](const SensitivityRange& r) {
            std::ostringstream oss;
            oss << "SensitivityRange(" << r.lower << ", " << r.upper << ")";
            return oss.str();
        });

    nb::class_<SolveResult>(m, "SolveResult",
                            "How a solve ended, and the solution it found. The dict-valued properties build a "
                            "new dict on each access; use value()/dual()/... (or result[var]) for single lookups.")
        .def_ro("status", &SolveResult::status)
        .def_ro("has_solution", &SolveResult::has_solution,
                "Whether a feasible solution came back -- possibly without being proven optimal, e.g. after a "
                "time limit. objective_value and the variable values are only meaningful when it did.")
        .def_ro("objective_value", &SolveResult::objective_value)
        .def_prop_ro("optimal", &SolveResult::optimal)
        .def_ro("variable_values", &SolveResult::variable_values)
        .def_ro("reduced_costs", &SolveResult::reduced_costs)
        .def_ro("duals", &SolveResult::duals)
        .def_ro("objective_coefficient_ranges", &SolveResult::objective_coefficient_ranges)
        .def_ro("variable_bound_ranges", &SolveResult::variable_bound_ranges)
        .def_ro("constraint_bound_ranges", &SolveResult::constraint_bound_ranges)
        .def(
            "value",
            [](const SolveResult& r, Variable var) { return lookup(r.variable_values, var, "no value for this variable"); },
            "var"_a)
        .def(
            "__getitem__",
            [](const SolveResult& r, Variable var) { return lookup(r.variable_values, var, "no value for this variable"); },
            "var"_a)
        .def(
            "values",
            [](const SolveResult& r, nb::handle vars) -> nb::object {
                auto value_of = [&](nb::handle v) {
                    return lookup(r.variable_values, nb::cast<Variable>(v), "no value for this variable");
                };
                if (nb::isinstance<nb::dict>(vars)) {
                    nb::dict result;
                    for (auto [k, v] : nb::borrow<nb::dict>(vars)) result[k] = value_of(v);
                    return result;
                }
                nb::list result;
                for (nb::handle v : nb::borrow<nb::iterable>(vars)) result.append(value_of(v));
                return result;
            },
            "vars"_a,
            "The value of every variable in `vars`: a dict from the same keys for a dict (e.g. what add_vars "
            "returned), a list in the same order for any other iterable.")
        .def(
            "reduced_cost",
            [](const SolveResult& r, Variable var) { return lookup(r.reduced_costs, var, "no reduced cost for this variable"); },
            "var"_a)
        .def(
            "dual",
            [](const SolveResult& r, ConstraintRef c) { return lookup(r.duals, c, "no dual for this constraint"); },
            "constraint"_a)
        .def(
            "objective_coefficient_range",
            [](const SolveResult& r, Variable var) {
                return lookup(r.objective_coefficient_ranges, var, "no objective coefficient range for this variable");
            },
            "var"_a)
        .def(
            "variable_bound_range",
            [](const SolveResult& r, Variable var) {
                return lookup(r.variable_bound_ranges, var, "no bound range for this variable");
            },
            "var"_a)
        .def(
            "constraint_bound_range",
            [](const SolveResult& r, ConstraintRef c) {
                return lookup(r.constraint_bound_ranges, c, "no bound range for this constraint");
            },
            "constraint"_a)
        .def("__repr__", [](const SolveResult& r) {
            std::ostringstream oss;
            oss << "SolveResult(status=" << to_string(r.status);
            if (r.has_solution) oss << ", objective_value=" << r.objective_value;
            oss << ")";
            return oss.str();
        });

    nb::class_<SolverProgress>(m, "SolverProgress", "A snapshot of solver progress, passed to a progress callback.")
        .def_ro("elapsed_seconds", &SolverProgress::elapsed_seconds)
        .def_ro("iterations", &SolverProgress::iterations)
        .def_ro("objective_value", &SolverProgress::objective_value)
        .def_ro("best_bound", &SolverProgress::best_bound, "MIP only; None otherwise.")
        .def_ro("gap", &SolverProgress::gap, "MIP only; None otherwise.")
        .def_ro("node_count", &SolverProgress::node_count, "MIP only; None otherwise.");

    nb::class_<PySolver>(m, "Solver",
                         "A solver backend bound to one Model, built by create_solver. Changes made through its "
                         "setters act on the backend's own copy of the model, not on the Model.")
        .def(
            "solve",
            [](PySolver& self) {
                *self.callback_error = nullptr;
                SolveResult result;
                {
                    nb::gil_scoped_release release;
                    result = self.solver->solve();
                }
                if (*self.callback_error) {
                    std::exception_ptr error = std::exchange(*self.callback_error, nullptr);
                    std::rethrow_exception(error);
                }
                return result;
            },
            "Solve, releasing the GIL while the backend runs.")
        .def(
            "set_progress_callback",
            [](PySolver& self, std::optional<nb::callable> callback) {
                if (!callback) {
                    self.solver->set_progress_callback({});
                    return;
                }
                // Held behind a shared_ptr so the backend copying the
                // std::function (GIL released, mid-solve) never touches the
                // callable's reference count; the last copy drops it with
                // the GIL held.
                std::shared_ptr<nb::object> fn(new nb::object(std::move(*callback)), [](nb::object* f) {
                    nb::gil_scoped_acquire acquire;
                    delete f;
                });
                std::shared_ptr<std::exception_ptr> error = self.callback_error;
                self.solver->set_progress_callback([fn, error](const SolverProgress& progress) {
                    nb::gil_scoped_acquire acquire;
                    try {
                        nb::object keep_going = (*fn)(progress);
                        return keep_going.is_none() || nb::cast<bool>(keep_going);
                    } catch (...) {
                        *error = std::current_exception();
                        return false;
                    }
                });
            },
            "callback"_a.none(),
            "Call `callback(progress)` periodically during solve(); returning False stops the solve early (None "
            "or True continues). An exception it raises also stops the solve, and is re-raised from solve(). "
            "Pass None to clear it.")
        .def("set_sensitivity_ranging",
             [](PySolver& self, bool enabled) { self.solver->set_sensitivity_ranging(enabled); }, "enabled"_a,
             "Whether solve() also computes sensitivity ranging (the *_ranges results) for an LP. Off by default; "
             "duals and reduced costs come back either way.")
        .def("objective_coefficient",
             [](const PySolver& self, Variable var) { return self.solver->objective_coefficient(var); }, "var"_a)
        .def("set_objective_coefficient",
             [](PySolver& self, Variable var, double c) { self.solver->set_objective_coefficient(var, c); }, "var"_a,
             "coeff"_a)
        .def("constraint_coefficient",
             [](const PySolver& self, ConstraintRef c, Variable var) {
                 return self.solver->constraint_coefficient(c, var);
             },
             "constraint"_a, "var"_a)
        .def("set_constraint_coefficient",
             [](PySolver& self, ConstraintRef c, Variable var, double coeff) {
                 self.solver->set_constraint_coefficient(c, var, coeff);
             },
             "constraint"_a, "var"_a, "coeff"_a)
        .def("constraint_lower", [](const PySolver& self, ConstraintRef c) { return self.solver->constraint_lower(c); },
             "constraint"_a)
        .def("set_constraint_lower",
             [](PySolver& self, ConstraintRef c, double v) { self.solver->set_constraint_lower(c, to_bound(v)); },
             "constraint"_a, "lower"_a)
        .def("constraint_upper", [](const PySolver& self, ConstraintRef c) { return self.solver->constraint_upper(c); },
             "constraint"_a)
        .def("set_constraint_upper",
             [](PySolver& self, ConstraintRef c, double v) { self.solver->set_constraint_upper(c, to_bound(v)); },
             "constraint"_a, "upper"_a)
        .def("variable_lower", [](const PySolver& self, Variable var) { return self.solver->variable_lower(var); },
             "var"_a)
        .def("set_variable_lower",
             [](PySolver& self, Variable var, double v) { self.solver->set_variable_lower(var, to_bound(v)); },
             "var"_a, "lower"_a)
        .def("variable_upper", [](const PySolver& self, Variable var) { return self.solver->variable_upper(var); },
             "var"_a)
        .def("set_variable_upper",
             [](PySolver& self, Variable var, double v) { self.solver->set_variable_upper(var, to_bound(v)); },
             "var"_a, "upper"_a)
        .def("variable_kind", [](const PySolver& self, Variable var) { return self.solver->variable_kind(var); },
             "var"_a)
        .def("set_variable_kind",
             [](PySolver& self, Variable var, VarKind kind) { self.solver->set_variable_kind(var, kind); }, "var"_a,
             "kind"_a)
        .def(
            "write_model",
            [](const PySolver& self, nb::handle path) {
                nb::object fspath = nb::module_::import_("os").attr("fspath")(path);
                self.solver->write_model(nb::cast<std::string>(fspath));
            },
            "path"_a,
            "Write the backend's current model to `path`, in the format its extension names (.lp or .mps).")
        .def(
            "set_initial_solution",
            [](PySolver& self, const std::unordered_map<Variable, double>& values) {
                self.solver->set_initial_solution(values);
            },
            "values"_a,
            "Give the next solve() a starting point: a dict from Variable to value, for some or all variables.");

    m.def(
        "create_solver",
        [](const Model& model, const std::string& name, const std::optional<nb::dict>& options) {
            return PySolver{create_solver(model, name, to_options(options))};
        },
        "model"_a, "name"_a = "highs", "options"_a = nb::none(),
        "Build a Solver for `model` with the backend registered as `name` (\"highs\" or \"scip\"). `options` "
        "maps the backend's own option names to values -- a bool, int, float, or str each.");
}

}  // namespace saiopt::python
