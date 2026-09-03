#include "saiopt/model.hpp"

#include "saiopt/highs_solver.hpp"

namespace saiopt {

Variable Model::add_variable(std::string name, double lower, double upper, VariableType type) {
    std::size_t index = var_lower_.size();
    var_names_.push_back(std::move(name));
    var_lower_.push_back(lower);
    var_upper_.push_back(upper);
    var_types_.push_back(type);
    return Variable(index);
}

void Model::add_constraint(Constraint constraint, std::string name) {
    constraints_.push_back(std::move(constraint));
    constraint_names_.push_back(std::move(name));
}

void Model::set_objective(LinearExpression expr, ObjectiveSense sense) {
    objective_.expression = std::move(expr);
    objective_.sense = sense;
}

SolveResult Model::solve() const {
    return HighsSolver{}.solve(*this);
}

}  // namespace saiopt
