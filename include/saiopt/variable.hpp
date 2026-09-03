#pragma once

#include <cstddef>

namespace saiopt {

enum class VariableType { Continuous, Integer, Binary };

class Model;

// Lightweight handle to a variable owned by a Model. Cheap to copy; carries
// no state of its own beyond the index into its owning Model's storage.
class Variable {
public:
    Variable() = default;

    std::size_t index() const noexcept { return index_; }

    friend bool operator==(const Variable& a, const Variable& b) noexcept {
        return a.index_ == b.index_;
    }
    friend bool operator!=(const Variable& a, const Variable& b) noexcept {
        return !(a == b);
    }

private:
    friend class Model;
    explicit Variable(std::size_t index) : index_(index) {}

    std::size_t index_ = 0;
};

}  // namespace saiopt
