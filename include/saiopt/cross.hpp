#pragma once

#include <tuple>
#include <type_traits>
#include <vector>

namespace saiopt {

namespace detail {

// One 1-tuple per element of `range` -- the seed for building up a flat
// multi-dimensional product one range at a time (see cross_extend).
template <typename Range>
auto cross_seed(const Range& range) {
    using T = std::decay_t<decltype(*std::begin(range))>;
    std::vector<std::tuple<T>> out;
    for (const auto& v : range) out.emplace_back(v);
    return out;
}

// Extends an existing flat-tuple product by one more range: every
// existing tuple combined with every element of `range`, appending each
// new element onto the end -- so the result stays a flat
// std::tuple<Ts..., T>, never std::tuple<std::tuple<Ts...>, T>, no
// matter how many ranges cross() folds over.
template <typename... Ts, typename Range>
auto cross_extend(const std::vector<std::tuple<Ts...>>& product, const Range& range) {
    using T = std::decay_t<decltype(*std::begin(range))>;
    std::vector<std::tuple<Ts..., T>> out;
    for (const auto& t : product) {
        for (const auto& v : range) {
            out.push_back(std::tuple_cat(t, std::make_tuple(v)));
        }
    }
    return out;
}

// Folds cross_extend over the remaining ranges one at a time; each step
// grows the tuple's arity by one, so this recurses at compile time
// (via if constexpr on the remaining pack size) rather than looping.
template <typename Product, typename Range, typename... Rest>
auto cross_fold(Product product, const Range& range, const Rest&... rest) {
    auto extended = cross_extend(product, range);
    if constexpr (sizeof...(Rest) == 0) {
        return extended;
    } else {
        return cross_fold(std::move(extended), rest...);
    }
}

}  // namespace detail

// The cartesian product of two or more ranges, as a flat
// std::vector<std::tuple<T1, T2, ..., TN>> -- one tuple per combination,
// in the order the ranges are given, with no nesting even past two
// ranges (cross(a, b, c) gives std::tuple<A, B, C>, never
// std::tuple<std::tuple<A, B>, C>). Used to build a multi-dimensional
// index for add_vars / add_constraints / add_blocks[_scaled] (and their
// SAIOPT_* macros):
//
//   auto routes = cross(warehouses, customers);
//   SAIOPT_VARS(model, x, routes, ([](const auto&) { return VariableDef::continuous(); }));
//   x[{w, c}]  // subscript with a flat tuple matching cross()'s order
template <typename Range1, typename Range2, typename... Ranges>
auto cross(const Range1& r1, const Range2& r2, const Ranges&... rest) {
    return detail::cross_fold(detail::cross_seed(r1), r2, rest...);
}

}  // namespace saiopt
