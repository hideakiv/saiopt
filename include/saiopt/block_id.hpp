#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace saiopt {

// Process-wide unique identifier for a Block, assigned on construction.
// Distinguishes which block's local storage a Variable/ConstraintRef
// indexes into, since every block keeps its own storage.
class BlockId {
public:
    BlockId() : value_(next()) {}

    friend bool operator==(const BlockId&, const BlockId&) noexcept = default;

    std::uint64_t raw() const noexcept { return value_; }

private:
    static std::uint64_t next() {
        static std::atomic<std::uint64_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t value_;
};

namespace detail {

// The splitmix64 finalizer: a bijection on 64-bit values that spreads any
// difference in its input across every bit of its output.
inline std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// Hash of an item numbered `index` within block `block`, for Variable's
// and ConstraintRef's std::hash. Every block numbers its own items from
// 0, so the block has to be mixed in well -- a simple additive combine
// let small ids and indices cancel out, and 24 blocks of 40,001
// variables shared only 41,478 distinct hashes that way, turning every
// lookup into a walk down a 24-long bucket chain. Only the block is
// mixed, though, and the index added on afterward: a block's items then
// land in consecutive buckets, which keeps walking one block's items in
// order (as flattening does) cache-friendly, while different blocks start
// at unrelated places in the table.
inline std::uint64_t hash_in_block(std::uint64_t block, std::uint64_t index) noexcept {
    return mix64(block) + index;
}

// Hash of an arbitrary pair of hashes, for VariablePair.
inline std::uint64_t hash_pair(std::uint64_t a, std::uint64_t b) noexcept {
    return mix64(a * 0x9e3779b97f4a7c15ULL + b);
}

}  // namespace detail

}  // namespace saiopt

template <>
struct std::hash<saiopt::BlockId> {
    std::size_t operator()(const saiopt::BlockId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.raw());
    }
};
