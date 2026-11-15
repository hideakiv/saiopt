#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bindings.hpp"
#include "saiopt/indexed.hpp"
#include "saiopt/infinity.hpp"
#include "saiopt/var_spec.hpp"

namespace saiopt::python {

bool operator<(const PyKey& a, const PyKey& b) {
    int result = PyObject_RichCompareBool(a.obj.ptr(), b.obj.ptr(), Py_LT);
    if (result < 0) throw nb::python_error();
    return result == 1;
}

namespace {

void format_key(std::ostream& os, nb::handle h) {
    if (PyUnicode_Check(h.ptr())) {
        os << nb::cast<std::string>(h);
    } else if (PyLong_Check(h.ptr()) && !PyBool_Check(h.ptr())) {
        int overflow = 0;
        long long value = PyLong_AsLongLongAndOverflow(h.ptr(), &overflow);
        if (overflow == 0) {
            os << value;
        } else {
            os << nb::str(h).c_str();
        }
    } else if (PyFloat_Check(h.ptr())) {
        os << PyFloat_AsDouble(h.ptr());
    } else if (PyTuple_Check(h.ptr())) {
        os << "(";
        nb::tuple t = nb::borrow<nb::tuple>(h);
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (i > 0) os << ", ";
            format_key(os, t[i]);
        }
        os << ")";
    } else {
        os << nb::str(h).c_str();
    }
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const PyKey& key) {
    format_key(os, key.obj);
    return os;
}

PyBlock PyBlock::owning(Block block) {
    PyBlock handle;
    handle.owned_ = std::make_unique<Block>(std::move(block));
    return handle;
}

PyBlock PyBlock::view(nb::object root, std::vector<std::string> path) {
    PyBlock handle;
    handle.root_ = std::move(root);
    handle.path_ = std::move(path);
    return handle;
}

Block& PyBlock::get() {
    if (owned_) return *owned_;
    if (consumed_ || !root_.is_valid()) {
        throw std::runtime_error(
            "this Block was moved into another block by add_block/add_blocks -- reach it through its "
            "parent instead, e.g. parent.block(name)");
    }
    Block* block = nb::isinstance<Model>(root_) ? nb::cast<Model&>(root_).block(path_.front())
                                                : nb::cast<PyBlock&>(root_).get().block(path_.front());
    for (std::size_t i = 1; block && i < path_.size(); ++i) block = block->block(path_[i]);
    if (!block) throw std::runtime_error("this Block handle's path no longer leads to a block");
    return *block;
}

PyBlock PyBlock::child(const std::string& name) {
    if (owned_) return view(nb::find(this), {name});
    get();  // fails early, and clearly, if this handle is already stale
    std::vector<std::string> path = path_;
    path.push_back(name);
    return view(root_, std::move(path));
}

Block PyBlock::take() {
    if (!owned_) {
        throw std::runtime_error(consumed_ ? "this Block was already moved into another block"
                                           : "only a standalone Block() can be added to a parent; this one is "
                                             "already part of a tree");
    }
    Block block = std::move(*owned_);
    owned_.reset();
    consumed_ = true;
    return block;
}

namespace {

Model& target(Model& model) { return model; }
Block& target(PyBlock& handle) { return handle.get(); }

PyBlock child_of(Model& model, const std::string& name) { return PyBlock::view(nb::find(&model), {name}); }
PyBlock child_of(PyBlock& handle, const std::string& name) { return handle.child(name); }

std::vector<PyKey> collect_keys(nb::iterable keys) {
    std::vector<PyKey> result;
    for (nb::handle k : keys) result.push_back(PyKey{nb::borrow(k)});
    return result;
}

template <typename T>
T per_key(const nb::object& value, const PyKey& key) {
    return PyCallable_Check(value.ptr()) ? nb::cast<T>(value(key.obj)) : nb::cast<T>(value);
}

// Throws if moving `child`'s Block out would pull the rug out from under
// `parent_obj` -- adding a block into itself or into one of its own
// descendants.
void check_not_ancestor(nb::handle parent_obj, PyBlock& child) {
    nb::handle child_obj = nb::find(&child);
    if (parent_obj.is(child_obj)) throw std::runtime_error("a Block can't be added to itself");
    if (nb::isinstance<PyBlock>(parent_obj) && nb::cast<PyBlock&>(parent_obj).rooted_at(child_obj)) {
        throw std::runtime_error("a Block can't be added to one of its own descendants");
    }
}

std::string indexed_name(const std::string& name, const PyKey& key) { return detail::indexed_name(name, key); }

// Re-keys a typed group by its items' values, then walks them in
// declaration order (`ordered`) -- so the resulting dict iterates in the
// order the group was declared, not the order its keys sort in.
template <typename V, typename Hash = std::hash<V>>
std::vector<std::pair<PyKey, V>> in_declaration_order(const Indexed<PyKey, V>& typed, const std::vector<V>& ordered) {
    std::unordered_map<V, PyKey, Hash> key_of;
    for (const auto& [k, v] : typed) key_of.emplace(v, k);
    std::vector<std::pair<PyKey, V>> result;
    result.reserve(ordered.size());
    for (const V& v : ordered) result.emplace_back(key_of.at(v), v);
    return result;
}

// A group of variables or constraints as a dict from key to item, in
// declaration order. `typed<K>()` is the group as an Indexed<K, V>, and
// throws if it wasn't declared with key type K. A group declared from
// Python is keyed by PyKey; one declared in C++ (such as add_piecewise's
// "lambda") by a C++ type, which converts to the matching Python key.
template <typename V, typename Typed>
nb::dict group_as_dict(const std::vector<V>& ordered, Typed typed) {
    std::unordered_map<V, nb::object> key_of;
    auto keyed_by = [&]<typename K>() {
        try {
            for (const auto& [k, v] : typed.template operator()<K>()) {
                if constexpr (std::is_same_v<K, PyKey>) {
                    key_of.emplace(v, k.obj);
                } else {
                    key_of.emplace(v, nb::cast(k));
                }
            }
            return true;
        } catch (const std::runtime_error&) {
            return false;
        }
    };
    if (!keyed_by.template operator()<PyKey>() && !keyed_by.template operator()<std::size_t>() &&
        !keyed_by.template operator()<int>() && !keyed_by.template operator()<std::string>()) {
        typed.template operator()<PyKey>();  // throws the key-type error
    }
    nb::dict result;
    for (const V& v : ordered) result[key_of.at(v)] = v;
    return result;
}

// What Block.add_piecewise returns: f(x), and the child block holding its
// auxiliary variables.
struct PyPiecewise {
    LinearExpression value;
    nb::object block;  // a PyBlock
};

LinearExpression linear_of(nb::handle expr, const char* what) {
    auto l = as_linear(expr);
    if (!l) throw nb::type_error((std::string(what) + " must be a number, a Variable, or a linear expression").c_str());
    return std::move(*l);
}

// The methods Model and Block share. `Self` is Model (used directly) or
// PyBlock (resolved through its path on every call; see PyBlock).
template <typename Self>
void bind_block_api(nb::class_<Self>& cls) {
    cls.def(
           "add_var",
           [](Self& self, std::string name, VarKind kind, std::pair<double, double> bounds) {
               return target(self).add_var(
                   std::move(name), detail::build_var_def(kind, to_bound(bounds.first), to_bound(bounds.second)));
           },
           "name"_a, nb::kw_only(), "kind"_a = VarKind::Continuous,
           "bounds"_a = std::pair<double, double>(-kInfinity, kInfinity),
           "Add a variable. `bounds` is a (lower, upper) pair, unbounded by default; a Binary variable is "
           "always [0, 1] regardless.")
        .def(
            "add_vars",
            [](Self& self, std::string name, nb::iterable keys, nb::object kind, nb::object bounds) {
                std::vector<PyKey> pykeys = collect_keys(keys);
                auto def = [&](const PyKey& k) {
                    auto [lower, upper] = per_key<std::pair<double, double>>(bounds, k);
                    return detail::build_var_def(per_key<VarKind>(kind, k), to_bound(lower), to_bound(upper));
                };
                Indexed<PyKey, Variable> vars = target(self).add_vars(std::move(name), pykeys, def);
                nb::dict result;
                for (const PyKey& k : pykeys) result[k.obj] = vars[k];
                return result;
            },
            "name"_a, "keys"_a, nb::kw_only(), "kind"_a = VarKind::Continuous,
            "bounds"_a = nb::make_tuple(-kInfinity, kInfinity),
            "Add one variable per key, each named `name[key]`, returning a dict from key to Variable. "
            "`kind` and `bounds` may each be a callable taking the key, for a per-key value.")
        .def(
            "add_constraint",
            [](Self& self, std::string name, const PyConstraint& c) {
                return target(self).add_constraint(std::move(name), c.constraint);
            },
            "name"_a, "constraint"_a)
        .def(
            "add_constraints",
            [](Self& self, std::string name, nb::iterable keys, nb::callable rule) {
                std::vector<PyKey> pykeys = collect_keys(keys);
                auto build = [&](const PyKey& k) { return nb::cast<const PyConstraint&>(rule(k.obj)).constraint; };
                Indexed<PyKey, ConstraintRef> refs = target(self).add_constraints(std::move(name), pykeys, build);
                nb::dict result;
                for (const PyKey& k : pykeys) result[k.obj] = refs[k];
                return result;
            },
            "name"_a, "keys"_a, "rule"_a,
            "Add one constraint per key, `rule(key)`, each named `name[key]`, returning a dict from key to "
            "ConstraintRef.")
        .def(
            "add_sos2",
            [](Self& self, std::string name, std::vector<Variable> vars) {
                target(self).add_sos2(std::move(name), std::move(vars));
            },
            "name"_a, "vars"_a,
            "Add a special ordered set of type 2 over `vars`, in the order given: at most two of them may be "
            "nonzero, and those two must be neighbours. The \"scip\" backend supports these; \"highs\" "
            "doesn't, and raises.")
        .def(
            "add_piecewise",
            [](Self& self, std::string name, nb::handle x, std::vector<double> points, nb::object values,
               PiecewiseMethod method) {
                std::vector<double> ys;
                if (PyCallable_Check(values.ptr())) {
                    ys.reserve(points.size());
                    for (double p : points) ys.push_back(nb::cast<double>(values(p)));
                } else {
                    ys = nb::cast<std::vector<double>>(values);
                }
                std::string child = name;
                Piecewise f = target(self).add_piecewise(std::move(name), linear_of(x, "x"), std::move(points),
                                                         std::move(ys), method);
                return PyPiecewise{std::move(f.value), nb::cast(child_of(self, child))};
            },
            "name"_a, "x"_a, "points"_a, "values"_a, nb::kw_only(), "method"_a = PiecewiseMethod::Incremental,
            "Model the piecewise linear function through (points[i], values[i]) at `x`, a Variable or linear "
            "expression. `values` may instead be a callable, `point -> value`. `points` must be "
            "non-decreasing; a repeated point makes a jump, and `x` is limited to [points[0], points[-1]]. "
            "The auxiliary variables go into a new child block `name`, and the constraint tying them to `x` "
            "into this block, also named `name`. Returns a Piecewise, whose `value` is f(x).")
        .def(
            "add_block",
            [](Self& self, const std::string& name, PyBlock& child, double multiplier) {
                check_not_ancestor(nb::find(&self), child);
                if (target(self).block(name)) {
                    throw std::runtime_error("a child block named \"" + name + "\" already exists in this block");
                }
                target(self).add_block_scaled(name, child.take(), multiplier);
                return child_of(self, name);
            },
            "name"_a, "block"_a, "multiplier"_a = 1.0,
            "Move a standalone `block` into this one as child `name`, its objective scaled by `multiplier`. "
            "`block` can't be used afterward; returns a handle to the attached child instead.")
        .def(
            "add_blocks",
            [](Self& self, const std::string& name, nb::iterable keys, nb::callable rule, nb::object multiplier) {
                nb::handle self_obj = nb::find(&self);
                std::vector<PyKey> pykeys = collect_keys(keys);
                auto build = [&](const PyKey& k) {
                    nb::object made = rule(k.obj);
                    PyBlock& child = nb::cast<PyBlock&>(made);
                    check_not_ancestor(self_obj, child);
                    return child.take();
                };
                auto scale = [&](const PyKey& k) { return per_key<double>(multiplier, k); };
                target(self).add_blocks_scaled(name, pykeys, build, scale);
                nb::dict result;
                for (const PyKey& k : pykeys) result[k.obj] = child_of(self, indexed_name(name, k));
                return result;
            },
            "name"_a, "keys"_a, "rule"_a, "multiplier"_a = 1.0,
            "Add one child per key, the standalone Block `rule(key)`, each named `name[key]`, returning a dict "
            "from key to child handle. `multiplier` may be a callable taking the key.")
        .def(
            "block",
            [](Self& self, const std::string& name) -> std::optional<PyBlock> {
                if (!target(self).block(name)) return std::nullopt;
                return child_of(self, name);
            },
            "name"_a, "The child block named `name`, or None.")
        .def(
            "blocks",
            [](Self& self, const std::string& name) {
                auto& b = target(self);
                Indexed<PyKey, Block*> typed = b.template blocks<PyKey>(name);
                std::vector<Block*> ordered = b.blocks(name);
                nb::dict result;
                for (const auto& [k, child] : in_declaration_order(typed, ordered)) {
                    result[k.obj] = child_of(self, indexed_name(name, k));
                }
                return result;
            },
            "name"_a, "The group of child blocks added by add_blocks(name, ...), as a dict from key to handle.")
        .def("var", [](Self& self, const std::string& name) { return target(self).var(name); }, "name"_a,
             "The variable named `name` in this block, or None.")
        .def(
            "vars",
            [](Self& self, const std::string& name) {
                auto& b = target(self);
                return group_as_dict(b.vars(name), [&]<typename K>() { return b.template vars<K>(name); });
            },
            "name"_a, "The group of variables added by add_vars(name, ...), as a dict from key to Variable.")
        .def("constraint", [](Self& self, const std::string& name) { return target(self).constraint(name); },
             "name"_a, "The constraint named `name` in this block, or None.")
        .def(
            "constraints",
            [](Self& self, const std::string& name) {
                auto& b = target(self);
                return group_as_dict(b.constraints(name),
                                     [&]<typename K>() { return b.template constraints<K>(name); });
            },
            "name"_a,
            "The group of constraints added by add_constraints(name, ...), as a dict from key to ConstraintRef.");
}

QuadraticExpression objective_of(nb::handle expr) {
    auto q = as_quadratic(expr);
    if (!q) throw nb::type_error("an objective must be a number or a linear or quadratic expression");
    return std::move(*q);
}

}  // namespace

void bind_blocks(nb::module_& m) {
    nb::enum_<ObjectiveSense>(m, "ObjectiveSense")
        .value("Minimize", ObjectiveSense::Minimize)
        .value("Maximize", ObjectiveSense::Maximize);

    nb::enum_<PiecewiseMethod>(m, "PiecewiseMethod", "How Block.add_piecewise models a piecewise linear function.")
        .value("Incremental", PiecewiseMethod::Incremental,
               "The default. A fill fraction per segment, plus binaries that fill the segments in order. "
               "Works with any backend that solves MILPs.")
        .value("Sos2", PiecewiseMethod::Sos2,
               "One weight per point, in an SOS2 set. Needs a backend with SOS2 support (\"scip\").");

    nb::class_<ConstraintRef>(m, "ConstraintRef", "A handle to a constraint owned by a Block or Model.")
        .def("__eq__", [](const ConstraintRef& a, const ConstraintRef& b) { return a == b; }, nb::is_operator())
        .def("__hash__", [](const ConstraintRef& c) { return std::hash<ConstraintRef>{}(c); })
        .def("__repr__", [](const ConstraintRef& c) {
            return "ConstraintRef(block=" + std::to_string(c.block().raw()) + ", index=" + std::to_string(c.index()) +
                   ")";
        });

    nb::class_<PyBlock> block(m, "Block",
                              "A composable sub-model: its own variables, constraints, and objective contribution, "
                              "plus any child blocks. Build one standalone with Block(), then move it into a "
                              "parent with add_block.");
    block.def("__init__", [](PyBlock* self) { new (self) PyBlock(PyBlock::owning(Block())); })
        .def(
            "set_objective", [](PyBlock& self, nb::handle expr) { self.get().set_objective(objective_of(expr)); },
            "expr"_a,
            "Set this block's own objective contribution, which its parent adds to the total scaled by the "
            "multiplier this block was attached with.")
        .def(
            "serialize",
            [](PyBlock& self) {
                std::vector<std::byte> bytes = self.get().serialize();
                return nb::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            },
            "This block's whole structure as bytes, for Block.deserialize -- e.g. in another process.")
        .def_static(
            "deserialize",
            [](nb::bytes data) {
                const auto* begin = static_cast<const std::byte*>(data.data());
                return PyBlock::owning(Block::deserialize(std::vector<std::byte>(begin, begin + data.size())));
            },
            "data"_a);
    bind_block_api(block);

    nb::class_<PyPiecewise>(m, "Piecewise", "A piecewise linear function, as Block.add_piecewise returns it.")
        .def_ro("value", &PyPiecewise::value, "f(x), as a linear expression.")
        .def_ro("block", &PyPiecewise::block, nb::sig("def block(self) -> Block"),
                "The child block holding the auxiliary variables.");

    nb::class_<Model> model(m, "Model",
                            "The root of a Block tree, plus the objective's sense -- the thing a Solver solves.");
    model.def(nb::init<>())
        .def_static(
            "from_block",
            [](PyBlock& block, std::optional<ObjectiveSense> sense) {
                Block b = block.take();
                return sense ? Model(std::move(b), *sense) : Model(std::move(b));
            },
            "block"_a, "sense"_a = nb::none(),
            "A Model whose root is the standalone `block` (which can't be used afterward), keeping its own "
            "objective. Without `sense`, set_objective must still be called before solving.")
        .def(
            "set_objective",
            [](Model& self, ObjectiveSense sense, nb::handle expr) { self.set_objective(sense, objective_of(expr)); },
            "sense"_a, "expr"_a)
        .def("__str__", [](const Model& self) {
            std::ostringstream oss;
            self.print(oss);
            return oss.str();
        });
    bind_block_api(model);
}

}  // namespace saiopt::python
