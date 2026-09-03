#pragma once

#include <cstddef>

#include "saiopt/block_id.hpp"

namespace saiopt {

class Block;

// A lightweight, copyable handle to a constraint owned by a Block (or
// Model, which is itself a block). Only ever produced by
// Block::add_constraint.
class ConstraintRef {
public:
    BlockId block() const noexcept { return block_; }
    std::size_t index() const noexcept { return index_; }

    friend bool operator==(const ConstraintRef&, const ConstraintRef&) noexcept = default;

private:
    friend class Block;
    ConstraintRef(BlockId block, std::size_t index) : block_(block), index_(index) {}

    BlockId block_;
    std::size_t index_;
};

}  // namespace saiopt
