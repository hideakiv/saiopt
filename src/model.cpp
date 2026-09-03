#include "saiopt/model.hpp"

#include <stdexcept>

#include "saiopt/highs_solver.hpp"

namespace saiopt {

void Model::set_objective_coefficient(Variable var, double coeff) {
    auto scale = root_.find_scale(var.block(), 1.0);
    if (!scale) throw std::runtime_error("variable does not belong to this model");
    if (*scale == 0.0) {
        if (coeff != 0.0) {
            throw std::runtime_error(
                "variable's block has a cumulative multiplier of 0.0, so its effective "
                "objective coefficient can only be set to 0.0");
        }
        return;
    }
    Block* owner = root_.find(var.block());
    if (!owner) throw std::runtime_error("variable does not belong to this model");
    owner->set_objective_coefficient_local(var, coeff / *scale);
}

double Model::objective_coefficient_local(Variable var) const {
    const Block* owner = root_.find(var.block());
    if (!owner) throw std::runtime_error("variable does not belong to this model");
    return owner->objective_coefficient_local(var);
}

void Model::set_objective_coefficient_local(Variable var, double coeff) {
    Block* owner = root_.find(var.block());
    if (!owner) throw std::runtime_error("variable does not belong to this model");
    owner->set_objective_coefficient_local(var, coeff);
}

SolveResult Model::solve() const { return HighsSolver{}.solve(*this); }

}  // namespace saiopt
