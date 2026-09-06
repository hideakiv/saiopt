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

// Fallback label for a variable that has no name registered anywhere in
// the tree -- identifies it by its owning block and local index.
std::string fallback_label(Variable var) {
    return "#" + std::to_string(var.block().raw()) + "." + std::to_string(var.index());
}

std::string label_of(const std::unordered_map<Variable, std::string>& labels, Variable var) {
    auto it = labels.find(var);
    return it != labels.end() ? it->second : fallback_label(var);
}

std::string format_expression(const LinearExpression& expr,
                               const std::unordered_map<Variable, std::string>& labels) {
    std::vector<std::pair<std::string, double>> terms;
    terms.reserve(expr.terms().size());
    for (const auto& [var, coeff] : expr.terms()) {
        if (coeff != 0.0) terms.emplace_back(label_of(labels, var), coeff);
    }
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

    double c = expr.constant();
    if (c != 0.0 || first) {
        if (first) {
            oss << c;
        } else {
            oss << (c < 0.0 ? " - " : " + ") << std::abs(c);
        }
    }
    return oss.str();
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

// A signed term of the effective total objective: `text` is a single
// block's own local objective, or a whole already-joined child
// subtree's contribution wrapped/scaled to reflect its multiplier.
struct ObjectiveTerm {
    std::string text;  // magnitude only, no leading sign
    bool negative;
};

// Joins signed terms into one "a - b + c" string, the same way
// format_expression joins a LinearExpression's own terms.
std::string join_objective_terms(const std::vector<ObjectiveTerm>& terms) {
    if (terms.empty()) return "0";
    std::ostringstream oss;
    bool first = true;
    for (const auto& term : terms) {
        if (first) {
            if (term.negative) oss << "-";
        } else {
            oss << (term.negative ? " - " : " + ");
        }
        oss << term.text;
        first = false;
    }
    return oss.str();
}

}  // namespace

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

std::string Model::format_total_objective(const Block& block,
                                           const std::unordered_map<Variable, std::string>& labels) {
    std::vector<ObjectiveTerm> terms;
    if (!block.objective_.terms().empty() || block.objective_.constant() != 0.0) {
        terms.push_back({format_expression(block.objective_, labels), false});
    }
    for (const auto& child : block.children_) {
        std::string child_total = format_total_objective(child.block, labels);
        if (child_total == "0") continue;

        double magnitude = std::abs(child.multiplier);
        std::ostringstream term;
        if (magnitude != 1.0) term << magnitude << " * ";
        term << "(" << child_total << ")";
        terms.push_back({term.str(), child.multiplier < 0.0});
    }
    return join_objective_terms(terms);
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
    collect_labels(root_, "", labels);

    os << "model ("
       << (sense_ ? (*sense_ == ObjectiveSense::Minimize ? "minimize" : "maximize")
                  : "no objective")
       << ")\n";
    os << "objective: " << format_total_objective(root_, labels) << "\n";
    print_block(os, root_, labels, 0);
}

void Model::print() const { print(std::cout); }

}  // namespace saiopt
