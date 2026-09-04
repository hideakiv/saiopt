#pragma once

#include <cstddef>
#include <functional>

#include "saiopt/block_id.hpp"

namespace saiopt {

class Block;
class ModelFlattener;

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
    friend class ModelFlattener;
    ConstraintRef(BlockId block, std::size_t index) : block_(block), index_(index) {}

    BlockId block_;
    std::size_t index_;
};

}  // namespace saiopt

template <>
struct std::hash<saiopt::ConstraintRef> {
    std::size_t operator()(const saiopt::ConstraintRef& c) const noexcept {
        std::size_t h1 = std::hash<saiopt::BlockId>{}(c.block());
        std::size_t h2 = std::hash<std::size_t>{}(c.index());
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
