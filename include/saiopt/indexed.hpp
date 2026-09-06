#pragma once

#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "saiopt/constraint_ref.hpp"
#include "saiopt/variable.hpp"

namespace saiopt {

// A container mapping index values to the Variable/ConstraintRef/BlockRef
// created for them, as produced by Block::add_vars / Block::add_constraints
// / Block::add_blocks[_scaled] (and their Model-level mirrors).
//
// Backed by std::map (ordered, requiring K to support operator<) rather
// than an unordered_map, so multi-dimensional keys built with cross()
// (std::tuple) work without requiring a std::hash specialization.
template <typename K, typename V>
class Indexed {
public:
    static Indexed from_map(std::map<K, V> map) { return Indexed(std::move(map)); }

    const V* get(const K& key) const {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    auto begin() const { return map_.begin(); }
    auto end() const { return map_.end(); }

    std::size_t size() const noexcept { return map_.size(); }
    bool empty() const noexcept { return map_.empty(); }

    const V& operator[](const K& key) const {
        auto it = map_.find(key);
        if (it == map_.end()) throw std::out_of_range("no entry found for the given index");
        return it->second;
    }

private:
    explicit Indexed(std::map<K, V> map) : map_(std::move(map)) {}
    std::map<K, V> map_;
};

template <typename K>
using IndexedVar = Indexed<K, Variable>;
template <typename K>
using IndexedConstraint = Indexed<K, ConstraintRef>;

// Lets a cross()-built tuple key be formatted into a name, e.g.
// "region[(a, b, c)]". Declared here (before detail::indexed_name below)
// so ordinary unqualified lookup finds it at that template's point of
// definition.
template <typename... Ts>
std::ostream& operator<<(std::ostream& os, const std::tuple<Ts...>& t) {
    os << "(";
    std::apply(
        [&os](const auto&... elems) {
            std::size_t i = 0;
            ((os << elems << (++i < sizeof...(Ts) ? ", " : "")), ...);
        },
        t);
    return os << ")";
}

namespace detail {

// Formats `prefix[key]`, the per-item name saiopt's indexed group methods
// (and the SAIOPT_VARS macro) derive for each item of an index. `K` must be
// streamable via operator<<.
template <typename K>
std::string indexed_name(const std::string& prefix, const K& key) {
    std::ostringstream oss;
    oss << prefix << "[" << key << "]";
    return oss.str();
}

}  // namespace detail

}  // namespace saiopt
