#pragma once

#include "saiopt/expression.hpp"

namespace saiopt {

enum class ObjectiveSense { Minimize, Maximize };

struct Objective {
    LinearExpression expression;
    ObjectiveSense sense = ObjectiveSense::Minimize;
};

}  // namespace saiopt
