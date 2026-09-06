#include "saiopt/sparse_lp.hpp"

#include <functional>
#include <map>
#include <stdexcept>
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
        flattener.collect_vars(root);
        flattener.collect_constraints_and_objective(root, 1.0);
        return flattener.build(model);
    }

private:
    struct FlatConstraint {
        std::vector<std::pair<std::int64_t, double>> coefficients;
        double lower;
        double upper;
        ConstraintRef ref;
    };

    std::unordered_set<BlockId> collect_vars(const Block& block) {
        for (std::size_t i = 0; i < block.vars_.size(); ++i) {
            Variable variable(block.id_, i);
            std::int64_t column = static_cast<std::int64_t>(kinds_.size());
            const auto& vd = block.vars_[i];
            kinds_.push_back(vd.kind);
            lowers_.push_back(vd.lower);
            uppers_.push_back(vd.upper);
            var_order_.push_back(variable);
            columns_.emplace(variable, column);
        }

        std::unordered_set<BlockId> own_scope{block.id_};
        for (const auto& child : block.children_) {
            auto child_scope = collect_vars(child.block);
            own_scope.insert(child_scope.begin(), child_scope.end());
        }
        descendants_.emplace(block.id_, own_scope);
        return own_scope;
    }

    void collect_constraints_and_objective(const Block& block, double scale) {
        const auto& own_scope = descendants_.at(block.id_);
        auto resolve = [&](Variable var) -> std::int64_t {
            if (!own_scope.count(var.block())) {
                throw std::runtime_error(
                    "a block's constraints and objective may only reference variables that "
                    "belong to the block itself or to one of its attached sub-blocks");
            }
            return columns_.at(var);
        };

        for (std::size_t i = 0; i < block.constraints_.size(); ++i) {
            const Constraint& c = block.constraints_[i].constraint;
            FlatConstraint fc{{}, c.lower(), c.upper(), ConstraintRef(block.id_, i)};
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

        for (const auto& child : block.children_) {
            collect_constraints_and_objective(child.block, scale * child.multiplier);
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

        lp.row_lower.reserve(constraints_.size());
        lp.row_upper.reserve(constraints_.size());
        lp.row_constraint.reserve(constraints_.size());
        for (const auto& c : constraints_) {
            lp.row_lower.push_back(c.lower);
            lp.row_upper.push_back(c.upper);
            lp.row_constraint.push_back(c.ref);
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
    std::unordered_map<BlockId, std::unordered_set<BlockId>> descendants_;
    std::vector<FlatConstraint> constraints_;
    LinearExpression objective_;
};

SparseLp flatten(const Model& model) { return ModelFlattener::flatten(model); }

}  // namespace saiopt
