#include "saiopt/sparse_lp.hpp"

#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "saiopt/model.hpp"

namespace saiopt {

// Walks the Block tree, assigning every variable a global column,
// validating that every constraint/objective only references variables
// owned by its own block or one of that block's attached descendants, and
// aggregating each block's objective scaled by the product of multipliers
// from the root down to it.
class ModelFlattener {
public:
    static SparseLp flatten(const Model& model) {
        if (!model.sense_) {
            throw std::runtime_error("model has no objective set");
        }

        ModelFlattener flattener;
        const Block& root = model;
        flattener.collect_vars(root, "");
        flattener.collect_constraints_and_objective(root, 1.0, "");
        return flattener.build(model);
    }

private:
    struct FlatConstraint {
        std::vector<std::pair<std::int64_t, double>> coefficients;
        double lower;
        double upper;
        ConstraintRef ref;
        std::string name;
    };

    // `name` qualified by the chain of child-block names leading down to
    // it, e.g. "region[3].x".
    static std::string qualify(const std::string& prefix, const std::string& name) {
        return prefix.empty() ? name : prefix + "." + name;
    }

    // Each child block's qualified name, in children_ order.
    static std::vector<std::string> child_prefixes(const Block& block, const std::string& prefix) {
        std::vector<std::string> prefixes(block.children_.size());
        for (const auto& [name, index] : block.child_names_) prefixes[index] = qualify(prefix, name);
        return prefixes;
    }

    std::unordered_set<BlockId> collect_vars(const Block& block, const std::string& prefix) {
        std::vector<std::string> var_names(block.vars_.size());
        for (const auto& [name, var] : block.var_names_) var_names[var.index()] = qualify(prefix, name);

        for (std::size_t i = 0; i < block.vars_.size(); ++i) {
            Variable variable(block.id_, i);
            std::int64_t column = static_cast<std::int64_t>(kinds_.size());
            const auto& vd = block.vars_[i];
            kinds_.push_back(vd.kind);
            lowers_.push_back(vd.lower);
            uppers_.push_back(vd.upper);
            var_order_.push_back(variable);
            var_names_.push_back(std::move(var_names[i]));
            columns_.emplace(variable, column);
        }

        std::unordered_set<BlockId> own_scope{block.id_};
        std::vector<std::string> prefixes = child_prefixes(block, prefix);
        for (std::size_t i = 0; i < block.children_.size(); ++i) {
            auto child_scope = collect_vars(block.children_[i].block, prefixes[i]);
            own_scope.insert(child_scope.begin(), child_scope.end());
        }
        descendants_.emplace(block.id_, own_scope);
        return own_scope;
    }

    void collect_constraints_and_objective(const Block& block, double scale,
                                           const std::string& prefix) {
        const auto& own_scope = descendants_.at(block.id_);
        auto resolve = [&](Variable var) -> std::int64_t {
            if (!own_scope.count(var.block())) {
                throw std::runtime_error(
                    "a block's constraints and objective may only reference variables that "
                    "belong to the block itself or to one of its attached sub-blocks");
            }
            return columns_.at(var);
        };

        std::vector<std::string> constraint_names(block.constraints_.size());
        for (const auto& [name, cref] : block.constraint_names_) {
            constraint_names[cref.index()] = qualify(prefix, name);
        }

        for (std::size_t i = 0; i < block.constraints_.size(); ++i) {
            const Constraint& c = block.constraints_[i].constraint;
            FlatConstraint fc{{}, c.lower(), c.upper(), ConstraintRef(block.id_, i),
                              std::move(constraint_names[i])};
            fc.coefficients.reserve(c.expression().terms().size());
            for (const auto& [var, coeff] : c.expression().terms()) {
                fc.coefficients.emplace_back(resolve(var), coeff);
            }
            constraints_.push_back(std::move(fc));
        }

        for (const auto& [var, coeff] : block.objective_.terms()) {
            (void)coeff;
            resolve(var);
        }
        objective_ += block.objective_ * scale;

        std::vector<std::string> prefixes = child_prefixes(block, prefix);
        for (std::size_t i = 0; i < block.children_.size(); ++i) {
            const ChildBlock& child = block.children_[i];
            collect_constraints_and_objective(child.block, scale * child.multiplier, prefixes[i]);
        }
    }

    SparseLp build(const Model& model) const {
        SparseLp lp;
        std::size_t num_col = kinds_.size();
        lp.num_col = static_cast<std::int64_t>(num_col);
        lp.num_row = static_cast<std::int64_t>(constraints_.size());

        lp.col_cost.assign(num_col, 0.0);
        for (const auto& [var, coeff] : objective_.terms()) {
            lp.col_cost[static_cast<std::size_t>(columns_.at(var))] += coeff;
        }
        lp.offset = objective_.constant();
        lp.sense = (*model.sense_ == ObjectiveSense::Maximize) ? -1 : 1;

        lp.col_lower = lowers_;
        lp.col_upper = uppers_;
        lp.col_kind = kinds_;
        lp.col_variable = var_order_;
        lp.col_name = var_names_;

        lp.row_lower.reserve(constraints_.size());
        lp.row_upper.reserve(constraints_.size());
        lp.row_constraint.reserve(constraints_.size());
        for (const auto& c : constraints_) {
            lp.row_lower.push_back(c.lower);
            lp.row_upper.push_back(c.upper);
            lp.row_constraint.push_back(c.ref);
            lp.row_name.push_back(c.name);
        }

        // Build the constraint matrix column-by-column (CSC).
        std::vector<std::map<std::int64_t, double>> columns_by_var(num_col);
        for (std::size_t r = 0; r < constraints_.size(); ++r) {
            for (const auto& [col, coeff] : constraints_[r].coefficients) {
                if (coeff != 0.0) {
                    columns_by_var[static_cast<std::size_t>(col)][static_cast<std::int64_t>(r)] +=
                        coeff;
                }
            }
        }

        lp.a_start.reserve(num_col + 1);
        lp.a_start.push_back(0);
        for (const auto& column : columns_by_var) {
            for (const auto& [row, coeff] : column) {
                lp.a_index.push_back(row);
                lp.a_value.push_back(coeff);
            }
            lp.a_start.push_back(static_cast<std::int64_t>(lp.a_index.size()));
        }

        return lp;
    }

    std::unordered_map<Variable, std::int64_t> columns_;
    std::vector<VarKind> kinds_;
    std::vector<double> lowers_;
    std::vector<double> uppers_;
    std::vector<Variable> var_order_;
    std::vector<std::string> var_names_;
    std::unordered_map<BlockId, std::unordered_set<BlockId>> descendants_;
    std::vector<FlatConstraint> constraints_;
    LinearExpression objective_;
};

SparseLp flatten(const Model& model) { return ModelFlattener::flatten(model); }

std::vector<std::string> file_safe_names(const std::vector<std::string>& names) {
    std::vector<std::string> safe;
    safe.reserve(names.size());
    std::unordered_set<std::string> used;
    for (const std::string& name : names) {
        std::string base;
        base.reserve(name.size());
        for (char ch : name) {
            auto c = static_cast<unsigned char>(ch);
            if (std::isspace(c)) continue;
            if (ch == '[') {
                base += '(';
            } else if (ch == ']') {
                base += ')';
            } else if (std::isalnum(c) || std::strchr("_.(),~", ch) != nullptr) {
                base += ch;
            } else {
                base += '_';
            }
        }
        if (base.empty() || std::isdigit(static_cast<unsigned char>(base.front())) ||
            base.front() == '.') {
            base.insert(base.begin(), '_');
        }

        std::string candidate = base;
        for (int n = 2; used.count(candidate); ++n) candidate = base + "~" + std::to_string(n);
        used.insert(candidate);
        safe.push_back(std::move(candidate));
    }
    return safe;
}

}  // namespace saiopt
