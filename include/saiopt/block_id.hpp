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

}  // namespace saiopt

template <>
struct std::hash<saiopt::BlockId> {
    std::size_t operator()(const saiopt::BlockId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.raw());
    }
};
