#pragma once

#include <nanobind/nanobind.h>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "saiopt/block.hpp"
#include "saiopt/constraint.hpp"
#include "saiopt/expression.hpp"
#include "saiopt/model.hpp"

namespace saiopt::python {

namespace nb = nanobind;
using namespace nanobind::literals;

// An index key for add_vars/add_constraints/add_blocks given from Python:
// any Python object, ordered by Python's own `<` (so keys must be
// mutually comparable, the same requirement as sorted()) -- the one key
// type every indexed group declared from Python uses on the C++ side, so
// those groups register as ordinary typed groups there too.
struct PyKey {
    nb::object obj;

    friend bool operator<(const PyKey& a, const PyKey& b);
};

// Formats `key` the way C++ names an indexed item ("x[...]"): a str as
// itself, an int or float the way C++ streams one, a tuple as "(a, b)"
// with each element formatted the same way -- so a group declared from
// Python names its items exactly as the same group declared from C++
// would -- and anything else as its str().
std::ostream& operator<<(std::ostream& os, const PyKey& key);

// A Python-visible constraint. Wraps Constraint with one extra bit of
// state: whether it came from `a == b` on two Variables, the one case
// where it also has a truth value (whether a and b are the same
// variable) -- dicts compare keys with ==, so Variable has to stay
// usable as one even though == on it builds a constraint. Any other
// constraint has no truth value, which also turns Python's chained
// `lo <= expr <= hi` (evaluated as `(lo <= expr) and (expr <= hi)`) into
// an error rather than a silently dropped bound.
struct PyConstraint {
    Constraint constraint;
    std::optional<std::pair<Variable, Variable>> variable_equality;
};

// Python's handle to a Block. Either owns a standalone Block (one built
// with Block(), not yet attached anywhere), or is a view of a block
// inside some tree, found by walking `path` (child names) down from
// `root` (the Model or owning Block object) on every access -- a raw
// Block* into the tree would dangle as soon as a sibling's addition
// reallocates its parent's child storage. An owning handle becomes
// consumed once add_block/add_blocks moves its Block into a parent.
class PyBlock {
public:
    static PyBlock owning(Block block);
    static PyBlock view(nb::object root, std::vector<std::string> path);

    Block& get();

    // The handle for this block's child `name`.
    PyBlock child(const std::string& name);

    // Moves the owned Block out, leaving this handle consumed. Throws if
    // this handle isn't an unconsumed owning one.
    Block take();

    // Whether this is a view into the tree owned by `owner`.
    bool rooted_at(nb::handle owner) const { return root_.is_valid() && root_.is(owner); }

private:
    std::unique_ptr<Block> owned_;
    bool consumed_ = false;
    nb::object root_;
    std::vector<std::string> path_;
};

void bind_expressions(nb::module_& m);
void bind_blocks(nb::module_& m);
void bind_solvers(nb::module_& m);

// Number -> LinearExpression constant; Variable/LinearExpression -> itself.
std::optional<LinearExpression> as_linear(nb::handle h);

// as_linear, or a QuadraticExpression as itself.
std::optional<QuadraticExpression> as_quadratic(nb::handle h);

// A bound from Python, with +/-math.inf mapped onto saiopt's own
// +/-kInfinity.
double to_bound(double value);

}  // namespace saiopt::python
