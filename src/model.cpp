#include "saiopt/model.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

namespace saiopt {

namespace {

std::string format_bound(double value) {
    if (value <= -kInfinity) return "-inf";
    if (value >= kInfinity) return "+inf";
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string format_kind(VarKind kind) {
    switch (kind) {
        case VarKind::Continuous:
            return "continuous";
        case VarKind::Integer:
            return "integer";
        case VarKind::Binary:
            return "binary";
    }
    return "?";
}

// Fallback label for a variable with no name registered anywhere in the
// tree being printed -- every variable this tree actually owns has one
// (add_var requires it), so this only fires for a stray reference to a
// variable from outside the tree (flatten() rejects that at solve time;
// print() doesn't validate it, so it falls back here instead of crashing).
// Identifies the variable by its owning block and local index.
std::string fallback_label(Variable var) {
    return "#" + std::to_string(var.block().raw()) + "." + std::to_string(var.index());
}

std::string label_of(const std::unordered_map<Variable, std::string>& labels, Variable var) {
    auto it = labels.find(var);
    return it != labels.end() ? it->second : fallback_label(var);
}

// `var`'s globally qualified label (e.g. "sub.gc.z"), relative to
// `prefix` (e.g. "sub" -> "gc.z"). A label that isn't actually under
// `prefix` (a variable from outside the block whose objective is being
// formatted -- structurally possible, though flatten() rejects it at
// solve time) is returned unchanged rather than mangled.
std::string relative_label(const std::unordered_map<Variable, std::string>& labels, Variable var,
                            const std::string& prefix) {
    std::string full = label_of(labels, var);
    if (!prefix.empty() && full.size() > prefix.size() && full.compare(0, prefix.size(), prefix) == 0 &&
        full[prefix.size()] == '.') {
        return full.substr(prefix.size() + 1);
    }
    return full;
}

// Joins already-labeled `terms` (sorted by label) into "a + 2 b - c",
// followed by `constant` when it's nonzero or there are no terms at all.
std::string format_terms(std::vector<std::pair<std::string, double>> terms, double constant) {
    std::sort(terms.begin(), terms.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::ostringstream oss;
    bool first = true;
    for (const auto& [name, coeff] : terms) {
        double magnitude = std::abs(coeff);
        bool negative = coeff < 0.0;
        if (first) {
            if (negative) oss << "-";
        } else {
            oss << (negative ? " - " : " + ");
        }
        if (magnitude != 1.0) oss << magnitude << " ";
        oss << name;
        first = false;
    }

    if (constant != 0.0 || first) {
        if (first) {
            oss << constant;
        } else {
            oss << (constant < 0.0 ? " - " : " + ") << std::abs(constant);
        }
    }
    return oss.str();
}

std::vector<std::pair<std::string, double>> labeled_terms(
    const LinearExpression& expr, const std::unordered_map<Variable, std::string>& labels) {
    std::vector<std::pair<std::string, double>> terms;
    terms.reserve(expr.terms().size());
    for (const auto& [var, coeff] : expr.terms()) {
        if (coeff != 0.0) terms.emplace_back(label_of(labels, var), coeff);
    }
    return terms;
}

std::string format_expression(const LinearExpression& expr,
                               const std::unordered_map<Variable, std::string>& labels) {
    return format_terms(labeled_terms(expr, labels), expr.constant());
}

// Quadratic terms ("x^2", "x*y", their two labels in sorted order) come
// first, then the linear ones, then the constant.
std::string format_expression(const QuadraticExpression& expr,
                               const std::unordered_map<Variable, std::string>& labels) {
    std::vector<std::pair<std::string, double>> quadratic;
    for (const auto& [pair, coeff] : expr.quadratic_terms()) {
        if (coeff == 0.0) continue;
        std::string a = label_of(labels, pair.first());
        std::string b = label_of(labels, pair.second());
        if (pair.first() == pair.second()) {
            quadratic.emplace_back(a + "^2", coeff);
        } else {
            quadratic.emplace_back(a < b ? a + "*" + b : b + "*" + a, coeff);
        }
    }
    std::vector<std::pair<std::string, double>> linear = labeled_terms(expr.linear(), labels);
    if (quadratic.empty()) return format_terms(std::move(linear), expr.constant());

    std::string head = format_terms(std::move(quadratic), 0.0);
    if (linear.empty() && expr.constant() == 0.0) return head;
    std::string tail = format_terms(std::move(linear), expr.constant());
    return tail.front() == '-' ? head + " - " + tail.substr(1) : head + " + " + tail;
}

// Same as format_expression, but each variable is named relative to
// `prefix` instead of by its globally qualified label -- see
// relative_label above.
std::string format_expression_relative(const QuadraticExpression& expr,
                                        const std::unordered_map<Variable, std::string>& labels,
                                        const std::string& prefix) {
    std::unordered_map<Variable, std::string> relative;
    for (const auto& [var, coeff] : expr.linear().terms()) {
        relative.emplace(var, relative_label(labels, var, prefix));
    }
    for (const auto& [pair, coeff] : expr.quadratic_terms()) {
        relative.emplace(pair.first(), relative_label(labels, pair.first(), prefix));
        relative.emplace(pair.second(), relative_label(labels, pair.second(), prefix));
    }
    return format_expression(expr, relative);
}

std::string format_constraint(const Constraint& c,
                               const std::unordered_map<Variable, std::string>& labels) {
    std::string expr = format_expression(c.expression(), labels);
    double lo = c.lower();
    double hi = c.upper();
    bool has_lower = lo > -kInfinity;
    bool has_upper = hi < kInfinity;

    if (has_lower && has_upper && lo == hi) return expr + " == " + format_bound(hi);
    if (has_lower && has_upper) return format_bound(lo) + " <= " + expr + " <= " + format_bound(hi);
    if (has_lower) return expr + " >= " + format_bound(lo);
    if (has_upper) return expr + " <= " + format_bound(hi);
    return expr;
}

}  // namespace

std::string_view to_string(SolveStatus status) noexcept {
    switch (status) {
        case SolveStatus::Optimal:
            return "optimal";
        case SolveStatus::Infeasible:
            return "infeasible";
        case SolveStatus::Unbounded:
            return "unbounded";
        case SolveStatus::InfeasibleOrUnbounded:
            return "infeasible or unbounded";
        case SolveStatus::TimeLimit:
            return "time limit";
        case SolveStatus::IterationLimit:
            return "iteration limit";
        case SolveStatus::NodeLimit:
            return "node limit";
        case SolveStatus::SolutionLimit:
            return "solution limit";
        case SolveStatus::GapLimit:
            return "gap limit";
        case SolveStatus::ObjectiveLimit:
            return "objective limit";
        case SolveStatus::MemoryLimit:
            return "memory limit";
        case SolveStatus::Interrupted:
            return "interrupted";
        case SolveStatus::Error:
            return "error";
        case SolveStatus::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::ostream& operator<<(std::ostream& os, SolveStatus status) { return os << to_string(status); }

void Model::collect_labels(const Block& block, const std::string& prefix,
                            std::unordered_map<Variable, std::string>& labels) {
    for (const auto& [name, var] : block.var_names_) {
        labels.emplace(var, prefix.empty() ? name : prefix + "." + name);
    }
    for (const auto& [name, index] : block.child_names_) {
        std::string child_prefix = prefix.empty() ? name : prefix + "." + name;
        collect_labels(block.children_[index].block, child_prefix, labels);
    }
}

std::vector<std::string> Model::format_total_objective_lines(const Block& block,
                                                               const std::unordered_map<Variable, std::string>& labels,
                                                               const std::string& prefix, int indent) {
    std::vector<std::string> lines;
    std::string buffer;        // the line being built, without its indent prefix
    bool buffer_started = false;
    bool first = true;

    // The sign the next appended piece starts with: nothing for a
    // positive first term, "-" for a negative first term (both with no
    // trailing space, matching format_expression's own "-x" style), " + "
    // or " - " to join every term after that -- whether that next piece
    // continues the current line or starts a fresh one.
    auto sign = [&](bool negative) {
        std::string s = first ? (negative ? "-" : "") : (negative ? " - " : " + ");
        first = false;
        return s;
    };
    auto append = [&](const std::string& text) {
        buffer += text;
        buffer_started = true;
    };
    auto flush = [&]() {
        if (buffer_started) {
            lines.push_back(std::string(static_cast<std::size_t>(indent), ' ') + buffer);
            buffer.clear();
            buffer_started = false;
        }
    };

    if (!block.objective_.linear().terms().empty() || !block.objective_.quadratic_terms().empty() ||
        block.objective_.constant() != 0.0) {
        append(sign(false) + format_expression_relative(block.objective_, labels, prefix));
    }

    std::vector<std::string> child_names(block.children_.size());
    for (const auto& [name, index] : block.child_names_) child_names[index] = name;

    for (std::size_t i = 0; i < block.children_.size(); ++i) {
        const ChildBlock& child = block.children_[i];
        const std::string& child_name = child_names[i];
        std::string child_prefix = prefix.empty() ? child_name : prefix + "." + child_name;
        std::vector<std::string> child_lines =
            format_total_objective_lines(child.block, labels, child_prefix, indent + 2);
        if (child_lines.empty()) continue;  // this child contributes nothing -- no stray parentheses for it

        // "name.(" (with its sign and multiplier) finishes whatever line
        // is already in progress -- the block's own local objective, or a
        // still-pending close paren from the previous child -- rather
        // than starting a new one, so two terms that don't themselves
        // need a line break share one, the way "y + 2 a.(" or ") - b.("
        // do below. The multiplier, when not 1, is written the same way
        // format_expression writes a coefficient ("2 a.(", not
        // "2 * a.("), for the same reason: "a" already reads as a single
        // term being scaled, the same as any variable would.
        double magnitude = std::abs(child.multiplier);
        std::ostringstream head;
        head << sign(child.multiplier < 0.0);
        if (magnitude != 1.0) head << magnitude << " ";
        head << child_name << ".(";
        append(head.str());
        flush();

        for (const auto& line : child_lines) lines.push_back(line);

        // The close paren starts a new line at this block's own indent
        // (child_lines is already indented one level deeper than that) --
        // left pending rather than flushed immediately, so the next
        // sibling's term (if any) is appended right after it instead of
        // getting a line of its own.
        append(")");
    }
    flush();
    return lines;
}

void Model::print_block(std::ostream& os, const Block& block,
                         const std::unordered_map<Variable, std::string>& labels, int depth) {
    std::string ind(static_cast<std::size_t>(depth) * 2, ' ');

    if (!block.vars_.empty()) {
        std::vector<std::string> names(block.vars_.size());
        for (const auto& [name, var] : block.var_names_) names[var.index()] = name;

        os << ind << "variables:\n";
        for (std::size_t i = 0; i < block.vars_.size(); ++i) {
            const Block::VarData& vd = block.vars_[i];
            const std::string& name = names[i];
            os << ind << "  " << (name.empty() ? "#" + std::to_string(i) : name) << "  "
               << format_kind(vd.kind) << "  [" << format_bound(vd.lower) << ", "
               << format_bound(vd.upper) << "]\n";
        }
    }

    if (!block.constraints_.empty()) {
        std::vector<std::string> names(block.constraints_.size());
        for (const auto& [name, cref] : block.constraint_names_) names[cref.index()] = name;

        os << ind << "constraints:\n";
        for (std::size_t i = 0; i < block.constraints_.size(); ++i) {
            const std::string& name = names[i];
            os << ind << "  " << (name.empty() ? "#" + std::to_string(i) : name) << ": "
               << format_constraint(block.constraints_[i].constraint, labels) << "\n";
        }
    }

    if (!block.children_.empty()) {
        std::vector<std::string> names(block.children_.size());
        for (const auto& [name, index] : block.child_names_) names[index] = name;

        for (std::size_t i = 0; i < block.children_.size(); ++i) {
            const ChildBlock& cb = block.children_[i];
            os << ind << "block " << names[i] << ":\n";
            print_block(os, cb.block, labels, depth + 1);
        }
    }
}

void Model::print(std::ostream& os) const {
    std::unordered_map<Variable, std::string> labels;
    collect_labels(*this, "", labels);

    os << "model ("
       << (sense_ ? (*sense_ == ObjectiveSense::Minimize ? "minimize" : "maximize")
                  : "no objective")
       << ")\n";
    std::vector<std::string> objective_lines = format_total_objective_lines(*this, labels, "", 2);
    if (objective_lines.empty()) objective_lines.push_back("  0");
    os << "objective:\n";
    for (const auto& line : objective_lines) os << line << "\n";

    print_block(os, *this, labels, 0);
}

void Model::print() const { print(std::cout); }

}  // namespace saiopt
