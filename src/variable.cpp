#include "saiopt/variable.hpp"

#include "saiopt/var_spec.hpp"

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

namespace detail {

VariableDef build_var_def(VarKind kind, double lower, double upper) {
    switch (kind) {
        case VarKind::Continuous:
            return VariableDef::continuous().bound(lower, upper);
        case VarKind::Integer:
            return VariableDef::integer().bound(lower, upper);
        case VarKind::Binary:
            // Binary is always [0, 1] -- see VariableDef::binary()'s doc
            // comment -- so an explicit `bounds` here is ignored rather
            // than silently producing a non-binary [0,1] range.
            return VariableDef::binary();
    }
    return VariableDef::continuous().bound(lower, upper);
}

}  // namespace detail

}  // namespace saiopt
