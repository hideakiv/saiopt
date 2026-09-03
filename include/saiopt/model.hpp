#pragma once

#include <string>
#include <vector>

#include "saiopt/constraint.hpp"
#include "saiopt/objective.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

class Model;
struct SparseLp;
SparseLp flatten(const Model&);

struct SolveResult {
    bool optimal = false;
    double objective_value = 0.0;
    std::vector<double> variable_values;
    std::string status_message;
};

class Model {
public:
    Variable add_variable(std::string name, double lower = 0.0, double upper = kInfinity,
                           VariableType type = VariableType::Continuous);

    void add_constraint(Constraint constraint, std::string name = "");

    void set_objective(LinearExpression expr, ObjectiveSense sense = ObjectiveSense::Minimize);

    SolveResult solve() const;

    std::size_t num_variables() const noexcept { return var_lower_.size(); }
    std::size_t num_constraints() const noexcept { return constraints_.size(); }

    const std::string& variable_name(std::size_t index) const { return var_names_.at(index); }
    double variable_lower(std::size_t index) const { return var_lower_.at(index); }
    double variable_upper(std::size_t index) const { return var_upper_.at(index); }
    VariableType variable_type(std::size_t index) const { return var_types_.at(index); }

    const Constraint& constraint(std::size_t index) const { return constraints_.at(index); }
    const Objective& objective() const noexcept { return objective_; }

private:
    friend SparseLp flatten(const Model&);

    std::vector<std::string> var_names_;
    std::vector<double> var_lower_;
    std::vector<double> var_upper_;
    std::vector<VariableType> var_types_;

    std::vector<Constraint> constraints_;
    std::vector<std::string> constraint_names_;

    Objective objective_;
};

}  // namespace saiopt
