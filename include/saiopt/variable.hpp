#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "saiopt/block_id.hpp"
#include "saiopt/infinity.hpp"

namespace saiopt {

enum class VarKind { Continuous, Integer, Binary };

class Block;
class ModelFlattener;

// A lightweight, copyable handle to a variable owned by a Block (or Model,
// which is itself a block). Only ever produced by Block::add_var. A block's
// own constraints/objective may only reference a Variable that belongs to
// that block or one of its attached descendants -- checked when the model
// is solved.
class Variable {
public:
    BlockId block() const noexcept { return block_; }
    std::size_t index() const noexcept { return index_; }

    friend bool operator==(const Variable&, const Variable&) noexcept = default;

private:
    friend class Block;
    friend class ModelFlattener;
    Variable(BlockId block, std::size_t index) : block_(block), index_(index) {}

    BlockId block_;
    std::size_t index_;
};

// Fluent builder describing a variable's kind and bounds, to add to a
// Block/Model via Block::add_var(name, def) -- naming lives on add_var
// itself (a required parameter, like add_constraint/add_block), not here,
// so this carries no more than Constraint does.
//
// The default (from VariableDef::continuous() / VariableDef::integer()) is
// unbounded, ranging over (-inf, +inf). Use .bound(lower, upper) to
// restrict it -- e.g. .bound(0.0, kInfinity) for the classic LP
// nonnegativity convention. VariableDef::binary() is the one exception,
// always bounded to [0, 1].
class VariableDef {
public:
    static VariableDef continuous();
    static VariableDef integer();
    static VariableDef binary();

    // Set both bounds at once: `lower <= var <= upper`.
    VariableDef& bound(double lower, double upper) {
        lower_ = lower;
        upper_ = upper;
        return *this;
    }
    // Remove both bounds, making the variable range over (-inf, +inf) --
    // already the default; useful to undo an earlier .bound() call.
    VariableDef& free() {
        lower_ = -kInfinity;
        upper_ = kInfinity;
        return *this;
    }

private:
    friend class Block;

    VarKind kind_ = VarKind::Continuous;
    double lower_ = -kInfinity;
    double upper_ = kInfinity;
};

}  // namespace saiopt

template <>
struct std::hash<saiopt::Variable> {
    std::size_t operator()(const saiopt::Variable& v) const noexcept {
        std::size_t h1 = std::hash<saiopt::BlockId>{}(v.block());
        std::size_t h2 = std::hash<std::size_t>{}(v.index());
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
