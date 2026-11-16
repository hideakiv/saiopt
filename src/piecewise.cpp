#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "saiopt/block.hpp"
#include "saiopt/infinity.hpp"

namespace saiopt {

namespace {

// Neither infinite nor saiopt's own stand-in for it, kInfinity.
bool finite(double v) { return std::isfinite(v) && std::abs(v) < kInfinity; }

void check_breakpoints(const std::string& name, const std::vector<double>& points,
                       const std::vector<double>& values) {
    auto fail = [&](const std::string& why) {
        throw std::runtime_error("piecewise function \"" + name + "\": " + why);
    };
    if (points.size() != values.size()) {
        fail(std::to_string(points.size()) + " points but " + std::to_string(values.size()) + " values");
    }
    if (points.size() < 2) fail("needs at least two points");
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!finite(points[i]) || !finite(values[i])) {
            fail("point " + std::to_string(i) + " isn't finite");
        }
        if (i > 0 && points[i] < points[i - 1]) {
            fail("points must be non-decreasing, but point " + std::to_string(i) + " is less than point " +
                 std::to_string(i - 1));
        }
    }
}

std::vector<std::size_t> iota(std::size_t n) {
    std::vector<std::size_t> result(n);
    for (std::size_t i = 0; i < n; ++i) result[i] = i;
    return result;
}

VariableDef fraction() { return VariableDef::continuous().bound(0.0, 1.0); }

}  // namespace

// Sos2: weights lambda[i] in [0, 1], one per point, summing to 1, at most
// two neighbours nonzero. Then x = sum lambda[i] * points[i], and f(x) is
// the same combination of values.
//
// Incremental: delta[j] in [0, 1] is how much of segment j (from point j
// to point j + 1) is used, and binary full[j] says segment j is used up,
// with delta[j + 1] <= full[j] <= delta[j], so a segment can only start
// once the one before it is full. Then x = points[0] + sum delta[j] *
// (points[j + 1] - points[j]), and f(x) likewise.
Piecewise Block::add_piecewise(std::string name, LinearExpression x, std::vector<double> points,
                               std::vector<double> values, PiecewiseMethod method) {
    detail::check_name("a piecewise function", name);
    check_breakpoints(name, points, values);
    if (block(name)) throw std::runtime_error("a child block named \"" + name + "\" already exists in this block");
    if (constraint(name)) throw std::runtime_error("a constraint named \"" + name + "\" already exists in this block");

    Block aux;
    LinearExpression x_of;  // x, in terms of the auxiliary variables
    LinearExpression value;
    std::size_t n = points.size();

    if (method == PiecewiseMethod::Sos2) {
        auto lambda = aux.add_vars("lambda", iota(n), [](std::size_t) { return fraction(); });
        LinearExpression total;
        std::vector<Variable> members;
        for (std::size_t i = 0; i < n; ++i) {
            x_of += points[i] * lambda[i];
            value += values[i] * lambda[i];
            total += lambda[i];
            members.push_back(lambda[i]);
        }
        aux.add_constraint("convexity", total == 1.0);
        aux.add_sos2("sos2", std::move(members));
    } else {
        std::size_t segments = n - 1;
        auto delta = aux.add_vars("delta", iota(segments), [](std::size_t) { return fraction(); });
        auto full = aux.add_vars("full", iota(segments - 1), [](std::size_t) { return VariableDef::binary(); });
        x_of += points[0];
        value += values[0];
        for (std::size_t j = 0; j < segments; ++j) {
            x_of += (points[j + 1] - points[j]) * delta[j];
            value += (values[j + 1] - values[j]) * delta[j];
        }
        aux.add_constraints("full_only_if_filled", iota(segments - 1),
                            [&](std::size_t j) { return full[j] <= delta[j]; });
        aux.add_constraints("next_only_if_full", iota(segments - 1),
                            [&](std::size_t j) { return delta[j + 1] <= full[j]; });
    }

    BlockRef ref = add_block(name, std::move(aux));
    add_constraint(name, std::move(x) == std::move(x_of));
    Piecewise result{std::move(value), ref};
    piecewise_.emplace(std::move(name), result);
    return result;
}

std::optional<Piecewise> Block::piecewise(const std::string& name) const {
    auto it = piecewise_.find(name);
    if (it == piecewise_.end()) return std::nullopt;
    return it->second;
}

}  // namespace saiopt
