/**
 * @file serializer.h
 * @brief Serializer<T> 主模板 + 内建特化
 *
 * 为每个 C++ 值类型提供其与 Archive 的转换规则。
 * Serializer 是唯一需要用户编写的定制点。
 *
 * 语法糖：
 *   ar << value;   // 调用 Serializer<T>::write(ar, value)
 *   ar >> value;   // 调用 Serializer<T>::read(ar, value)
 */
#pragma once

#include "../core_export.h"
#include "archive.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mulan::core {

// ============================================================
// 数组最大元素数量（防恶意文件 / 检查溢出）
// 可在编译期通过定义宏覆盖： -DMULANGEO_MAX_ARRAY_SIZE=1000000
// 不同的 Archive 或 Serializer 可根据场景选择不同的限值。
// ============================================================

#ifndef MULANGEO_MAX_ARRAY_SIZE
#define MULANGEO_MAX_ARRAY_SIZE 10'000'000
#endif

// ============================================================
// Serializer<T> 主模板（用户需要特化此模板来支持自定义类型）
// ============================================================

template <typename T>
struct Serializer {
    static void write(OutputArchive& ar, const T& value) = delete;
    static ArchiveResult read(InputArchive& ar, T& value) = delete;
};

// ============================================================
// 语法糖：operator<< / operator>>
// ============================================================

template <typename T>
OutputArchive& OutputArchive::operator<<(const T& value) {
    Serializer<T>::write(*this, value);
    return *this;
}

template <typename T>
InputArchive& InputArchive::operator>>(T& value) {
    auto result = Serializer<T>::read(*this, value);
    if (!result) [[unlikely]] {
        setError(result.error().message);
    }
    return *this;
}

// ============================================================
// 内建特化：原始类型（直接透传 Archive 的 write/read）
// ============================================================

#define MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(Type)                                     \
    template <>                                                                         \
    struct CORE_API Serializer<Type> {                                                  \
        static void write(OutputArchive& ar, Type value) { ar.write(value); }           \
        static ArchiveResult read(InputArchive& ar, Type& out) { return ar.read(out); } \
    }

MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(int32_t);
MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(int64_t);
MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(float);
MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(double);
MULANGEO_DECLARE_PRIMITIVE_SERIALIZER(bool);

#undef MULANGEO_DECLARE_PRIMITIVE_SERIALIZER

// std::string 特化
template <>
struct CORE_API Serializer<std::string> {
    static void write(OutputArchive& ar, const std::string& value) { ar.write(std::string_view(value)); }
    static ArchiveResult read(InputArchive& ar, std::string& out) { return ar.read(out); }
};

// ============================================================
// 内建特化：枚举（按 underlying type 序列化）
// ============================================================

template <typename E>
    requires std::is_enum_v<E>
struct Serializer<E> {
    using Underlying = std::underlying_type_t<E>;

    static void write(OutputArchive& ar, E value) { Serializer<Underlying>::write(ar, static_cast<Underlying>(value)); }

    static ArchiveResult read(InputArchive& ar, E& out) {
        Underlying raw{};
        auto result = Serializer<Underlying>::read(ar, raw);
        if (!result)
            return result;
        out = static_cast<E>(raw);
        return {};
    }
};

// ============================================================
// 内建特化：std::vector<T>
// ============================================================

template <typename T>
struct Serializer<std::vector<T>> {
    static void write(OutputArchive& ar, const std::vector<T>& values) {
        ar.beginArray(static_cast<uint32_t>(values.size()));
        for (const auto& v : values) {
            Serializer<T>::write(ar, v);
        }
        ar.endArray();
    }

    static ArchiveResult read(InputArchive& ar, std::vector<T>& out) {
        uint32_t size = 0;
        auto result = ar.beginArray(size);
        if (!result)
            return result;

        if (size > MULANGEO_MAX_ARRAY_SIZE) [[unlikely]] {
            return std::unexpected(ArchiveError::outOfMemory(size));
        }

        out.resize(size);
        for (uint32_t i = 0; i < size; ++i) {
            result = Serializer<T>::read(ar, out[i]);
            if (!result)
                return result;
        }
        return ar.endArray();
    }
};

// ============================================================
// 内建特化：std::map<K, V>
// ============================================================

template <typename K, typename V>
struct Serializer<std::map<K, V>> {
    static void write(OutputArchive& ar, const std::map<K, V>& values) {
        ar.beginArray(static_cast<uint32_t>(values.size()));
        for (const auto& [k, v] : values) {
            ar.beginObject();
            ar.key("key");
            Serializer<K>::write(ar, k);
            ar.key("value");
            Serializer<V>::write(ar, v);
            ar.endObject();
        }
        ar.endArray();
    }

    static ArchiveResult read(InputArchive& ar, std::map<K, V>& out) {
        uint32_t size = 0;
        auto result = ar.beginArray(size);
        if (!result)
            return result;

        out.clear();
        for (uint32_t i = 0; i < size; ++i) {
            result = ar.beginObject();
            if (!result)
                return result;

            K k{};
            V v{};

            result = ar.key("key");
            if (!result)
                return result;
            result = Serializer<K>::read(ar, k);
            if (!result)
                return result;

            result = ar.key("value");
            if (!result)
                return result;
            result = Serializer<V>::read(ar, v);
            if (!result)
                return result;

            out.emplace(std::move(k), std::move(v));

            result = ar.endObject();
            if (!result)
                return result;
        }
        return ar.endArray();
    }
};

// ============================================================
// 内建特化：std::unordered_map<K, V>
// ============================================================

template <typename K, typename V>
struct Serializer<std::unordered_map<K, V>> {
    static void write(OutputArchive& ar, const std::unordered_map<K, V>& values) {
        ar.beginArray(static_cast<uint32_t>(values.size()));
        for (const auto& [k, v] : values) {
            ar.beginObject();
            ar.key("key");
            Serializer<K>::write(ar, k);
            ar.key("value");
            Serializer<V>::write(ar, v);
            ar.endObject();
        }
        ar.endArray();
    }

    static ArchiveResult read(InputArchive& ar, std::unordered_map<K, V>& out) {
        uint32_t size = 0;
        auto result = ar.beginArray(size);
        if (!result)
            return result;

        out.clear();
        out.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            result = ar.beginObject();
            if (!result)
                return result;

            K k{};
            V v{};

            result = ar.key("key");
            if (!result)
                return result;
            result = Serializer<K>::read(ar, k);
            if (!result)
                return result;

            result = ar.key("value");
            if (!result)
                return result;
            result = Serializer<V>::read(ar, v);
            if (!result)
                return result;

            out.emplace(std::move(k), std::move(v));

            result = ar.endObject();
            if (!result)
                return result;
        }
        return ar.endArray();
    }
};

// ============================================================
// 内建特化：std::optional<T>
// ============================================================

template <typename T>
struct Serializer<std::optional<T>> {
    static void write(OutputArchive& ar, const std::optional<T>& value) {
        ar.write(value.has_value());
        if (value.has_value()) {
            Serializer<T>::write(ar, *value);
        }
    }

    static ArchiveResult read(InputArchive& ar, std::optional<T>& out) {
        bool hasValue = false;
        auto result = Serializer<bool>::read(ar, hasValue);
        if (!result)
            return result;

        if (hasValue) {
            out.emplace();
            return Serializer<T>::read(ar, *out);
        } else {
            out.reset();
            return {};
        }
    }
};

// ============================================================
// 内建特化：std::variant<Ts...>
// ============================================================

namespace detail {

template <typename Archive, typename T, typename... Ts>
void writeVariantByIndex(Archive& /*ar*/, uint32_t /*targetIndex*/, uint32_t /*currentIndex*/,
                         const std::variant<Ts...>& /*value*/) {
    // 递归终止：不应到达此处
}

template <typename Archive, typename T, typename First, typename... Rest>
void writeVariantByIndex(Archive& ar, uint32_t targetIndex, uint32_t currentIndex,
                         const std::variant<T, Rest...>& value) {
    if (currentIndex == targetIndex) {
        Serializer<First>::write(ar, std::get<First>(value));
    } else {
        // 不匹配，继续递归（注意类型列表缩小为 Rest...）
        // 但 variant 的 index 和类型列表必须对齐，这里用相同的 variant 引用
        if constexpr (sizeof...(Rest) > 0) {
            writeVariantByIndex<Archive, T, Rest...>(ar, targetIndex, currentIndex + 1, value);
        }
    }
}

template <typename T>
void writeVariantHelper(OutputArchive& ar, const T& value) {
    ar.write(static_cast<uint32_t>(value.index()));
    writeVariantByIndex<OutputArchive, T, typename T::alternative_types...>(ar, static_cast<uint32_t>(value.index()), 0,
                                                                            value);
}

// 读取辅助：按 index 构造 variant
template <typename Variant, size_t I = 0>
ArchiveResult readVariantByIndex(InputArchive& ar, Variant& out, uint32_t targetIndex) {
    if constexpr (I >= std::variant_size_v<Variant>) {
        return std::unexpected(ArchiveError::corrupted("Variant index out of range"));
    } else {
        if (targetIndex == I) {
            using Alt = std::variant_alternative_t<I, Variant>;
            Alt val{};
            auto result = Serializer<Alt>::read(ar, val);
            if (!result)
                return result;
            out = std::move(val);
            return {};
        }
        return readVariantByIndex<Variant, I + 1>(ar, out, targetIndex);
    }
}

}  // namespace detail

template <typename... Ts>
struct Serializer<std::variant<Ts...>> {
    static void write(OutputArchive& ar, const std::variant<Ts...>& value) {
        ar.write(static_cast<uint32_t>(value.index()));
        detail::writeVariantHelper(ar, value);
    }

    static ArchiveResult read(InputArchive& ar, std::variant<Ts...>& out) {
        uint32_t index = 0;
        auto result = Serializer<uint32_t>::read(ar, index);
        if (!result)
            return result;

        if (index >= sizeof...(Ts)) [[unlikely]] {
            return std::unexpected(ArchiveError::corrupted("Variant index " + std::to_string(index) +
                                                           " exceeds type count " + std::to_string(sizeof...(Ts))));
        }
        return detail::readVariantByIndex(ar, out, index);
    }
};

// ============================================================
// 内建特化：std::unique_ptr<T>
// ============================================================

template <typename T>
struct Serializer<std::unique_ptr<T>> {
    static void write(OutputArchive& ar, const std::unique_ptr<T>& value) {
        ar.write(static_cast<bool>(value));
        if (value) {
            Serializer<T>::write(ar, *value);
        }
    }

    static ArchiveResult read(InputArchive& ar, std::unique_ptr<T>& out) {
        bool hasValue = false;
        auto result = Serializer<bool>::read(ar, hasValue);
        if (!result)
            return result;

        if (hasValue) {
            out = std::make_unique<T>();
            return Serializer<T>::read(ar, *out);
        } else {
            out.reset();
            return {};
        }
    }
};

}  // namespace mulan::core
