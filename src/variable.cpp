#include "saiopt/variable.hpp"

namespace saiopt {

VariableDef VariableDef::continuous() { return VariableDef{}; }

VariableDef VariableDef::integer() {
    VariableDef def;
    def.kind_ = VarKind::Integer;
    return def;
}

VariableDef VariableDef::binary() {
    VariableDef def;
    def.kind_ = VarKind::Binary;
    def.lower_ = 0.0;
    def.upper_ = 1.0;
    return def;
}

}  // namespace saiopt
