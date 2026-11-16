#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace saiopt {

// An index key in a form that doesn't depend on its C++ type: an integer,
// a floating-point number, a string, or a tuple of those. Block::serialize
// writes indexed groups' keys this way, and the typed lookups (vars<K>,
// constraints<K>, blocks<K>) turn them back into K after deserialize.
struct KeyValue {
    struct Tuple {
        std::vector<KeyValue> items;
        friend bool operator==(const Tuple&, const Tuple&) = default;
    };
    std::variant<std::int64_t, double, std::string, Tuple> value;

    friend bool operator==(const KeyValue&, const KeyValue&) = default;
};

// How keys of type K convert to and from KeyValue. Specialize it for a
// key type of your own to make groups keyed by it survive serialization;
// groups keyed by a type without a codec still serialize, but without
// their keys, so after deserialize only the untyped lookups (vars(name),
// ...) and lookups by full name work for them.
//
// A specialization defines `static std::optional<KeyValue> encode(const
// K&)` and `static std::optional<K> decode(const KeyValue&)`, each
// returning nullopt for a value it can't convert.
template <typename K, typename = void>
struct KeyCodec;

template <typename K>
concept HasKeyCodec = requires(const K& key, const KeyValue& value) {
    { KeyCodec<K>::encode(key) } -> std::same_as<std::optional<KeyValue>>;
    { KeyCodec<K>::decode(value) } -> std::same_as<std::optional<K>>;
};

template <typename K>
struct KeyCodec<K, std::enable_if_t<std::is_integral_v<K> && !std::is_same_v<K, bool>>> {
    static std::optional<KeyValue> encode(const K& key) {
        if constexpr (std::is_unsigned_v<K> && sizeof(K) >= sizeof(std::int64_t)) {
            if (key > static_cast<K>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
        }
        return KeyValue{static_cast<std::int64_t>(key)};
    }
    static std::optional<K> decode(const KeyValue& value) {
        const auto* i = std::get_if<std::int64_t>(&value.value);
        if (!i || !std::in_range<K>(*i)) return std::nullopt;
        return static_cast<K>(*i);
    }
};

template <typename K>
struct KeyCodec<K, std::enable_if_t<std::is_floating_point_v<K>>> {
    static std::optional<KeyValue> encode(const K& key) { return KeyValue{static_cast<double>(key)}; }
    static std::optional<K> decode(const KeyValue& value) {
        const auto* d = std::get_if<double>(&value.value);
        if (!d) return std::nullopt;
        return static_cast<K>(*d);
    }
};

template <>
struct KeyCodec<std::string> {
    static std::optional<KeyValue> encode(const std::string& key) { return KeyValue{key}; }
    static std::optional<std::string> decode(const KeyValue& value) {
        const auto* s = std::get_if<std::string>(&value.value);
        if (!s) return std::nullopt;
        return *s;
    }
};

namespace detail {

template <typename Tup, std::size_t... I>
std::optional<KeyValue> encode_tuple(const Tup& key, std::index_sequence<I...>) {
    KeyValue::Tuple tuple;
    bool ok = true;
    auto add = [&](const auto& item) {
        auto encoded = KeyCodec<std::decay_t<decltype(item)>>::encode(item);
        if (encoded) {
            tuple.items.push_back(std::move(*encoded));
        } else {
            ok = false;
        }
    };
    (add(std::get<I>(key)), ...);
    if (!ok) return std::nullopt;
    return KeyValue{std::move(tuple)};
}

template <typename Tup, std::size_t... I>
std::optional<Tup> decode_tuple(const KeyValue& value, std::index_sequence<I...>) {
    const auto* tuple = std::get_if<KeyValue::Tuple>(&value.value);
    if (!tuple || tuple->items.size() != sizeof...(I)) return std::nullopt;
    std::tuple<std::optional<std::tuple_element_t<I, Tup>>...> items{
        KeyCodec<std::tuple_element_t<I, Tup>>::decode(tuple->items[I])...};
    if (!(std::get<I>(items).has_value() && ...)) return std::nullopt;
    return Tup{*std::get<I>(items)...};
}

}  // namespace detail

// std::tuple and std::pair (cross()'s keys, for one), when every element
// type has a codec.
template <typename... Ts>
struct KeyCodec<std::tuple<Ts...>, std::enable_if_t<(HasKeyCodec<Ts> && ...)>> {
    static std::optional<KeyValue> encode(const std::tuple<Ts...>& key) {
        return detail::encode_tuple(key, std::index_sequence_for<Ts...>{});
    }
    static std::optional<std::tuple<Ts...>> decode(const KeyValue& value) {
        return detail::decode_tuple<std::tuple<Ts...>>(value, std::index_sequence_for<Ts...>{});
    }
};

template <typename A, typename B>
struct KeyCodec<std::pair<A, B>, std::enable_if_t<HasKeyCodec<A> && HasKeyCodec<B>>> {
    static std::optional<KeyValue> encode(const std::pair<A, B>& key) {
        return detail::encode_tuple(key, std::index_sequence<0, 1>{});
    }
    static std::optional<std::pair<A, B>> decode(const KeyValue& value) {
        return detail::decode_tuple<std::pair<A, B>>(value, std::index_sequence<0, 1>{});
    }
};

}  // namespace saiopt
