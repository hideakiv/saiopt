#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace saiopt {

// A named, ordered collection of index values, used to declare indexed
// Variables and constraints via Block::add_vars / add_constraints /
// add_blocks[_scaled] (and the SAIOPT_VARS / SAIOPT_CONSTRAINTS /
// SAIOPT_BLOCKS[_SCALED] macros).
template <typename T>
class Set {
public:
    Set() = default;
    Set(std::initializer_list<T> items) : items_(items) {}
    template <typename Range>
    explicit Set(const Range& items) : items_(std::begin(items), std::end(items)) {}

    Set(std::string name, std::initializer_list<T> items) : name_(std::move(name)), items_(items) {}
    template <typename Range>
    Set(std::string name, const Range& items)
        : name_(std::move(name)), items_(std::begin(items), std::end(items)) {}

    const std::optional<std::string>& name() const noexcept { return name_; }
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

    // The cartesian product of `this` and `other`, yielding a Set of
    // (T, U) pairs usable as multi-dimensional indices, e.g. x[{i, j}].
    template <typename U>
    Set<std::pair<T, U>> cross(const Set<U>& other) const {
        std::vector<std::pair<T, U>> items;
        items.reserve(items_.size() * other.items_.size());
        for (const auto& t : items_) {
            for (const auto& u : other.items_) {
                items.emplace_back(t, u);
            }
        }
        return Set<std::pair<T, U>>::from_vector(std::move(items));
    }

private:
    template <typename U>
    friend class Set;

    static Set from_vector(std::vector<T> items) {
        Set s;
        s.items_ = std::move(items);
        return s;
    }

    std::optional<std::string> name_;
    std::vector<T> items_;
};

}  // namespace saiopt
