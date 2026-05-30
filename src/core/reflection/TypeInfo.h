/**
 * @file TypeInfo.h
 * @brief 轻量类型标识，用于反射系统的类型查询
 *
 * TypeInfo 是对 std::type_index 的值语义包装，
 * 可用于作为 map key 或值传递。
 * 与序列化无关——仅服务 Reflection Layer。
 */
#pragma once

#include "../CoreExport.h"

#include <compare>
#include <functional>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>

namespace mulan::core {

// ============================================================
// TypeInfo — 轻量类型标识
// ============================================================

class CORE_API TypeInfo {
public:
    TypeInfo() = default;

    explicit TypeInfo(std::type_index idx)
        : m_index(idx) {}

    /// 从类型直接构造
    template<typename T>
    static TypeInfo of() {
        return TypeInfo(std::type_index(typeid(T)));
    }

    /// 类型名称（编译器生成的 demangled 名称）
    const char* name() const noexcept {
        return m_index.name();
    }

    /// 自定义的人类可读名称（由 TypeRegistry 注册时设置）
    void setDisplayName(std::string_view name) {
        m_displayName = name;
    }

    const std::string& displayName() const noexcept {
        return m_displayName;
    }

    bool operator==(const TypeInfo& other) const noexcept {
        return m_index == other.m_index;
    }

    bool operator!=(const TypeInfo& other) const noexcept {
        return m_index != other.m_index;
    }

    bool operator<(const TypeInfo& other) const noexcept {
        return m_index < other.m_index;
    }

    explicit operator bool() const noexcept {
        return m_index != std::type_index(typeid(void));
    }

    /// 获取底层 type_index（用于哈希等）
    const std::type_index& typeIndex() const noexcept {
        return m_index;
    }

private:
    std::type_index m_index{typeid(void)};
    std::string m_displayName;
};

} // namespace mulan::core

// ============================================================
// std::hash 特化，允许 TypeInfo 作为 unordered_map/unordered_set 的 key
// ============================================================

template<>
struct std::hash<mulan::core::TypeInfo> {
    size_t operator()(const mulan::core::TypeInfo& ti) const noexcept {
        return ti.typeIndex().hash_code();
    }
};
