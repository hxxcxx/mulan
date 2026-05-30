/**
 * @file TypeRegistry.h
 * @brief 全局类型注册表（Reflection Layer）
 *
 * TypeRegistry 管理 ClassInfo 的注册和查询。
 * 支持按类名字符串、TypeInfo、type_index 三种方式查询。
 * registerClass + registerProperty 两阶段注册。
 */
#pragma once

#include "../CoreExport.h"
#include "TypeInfo.h"
#include "Object.h"       // ClassInfo 完整定义

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace mulan::core {

// ============================================================
// PropertyAccess — 属性读写标志
// ============================================================

enum class PropertyAccess : uint8_t {
    ReadOnly  = 0x01,
    WriteOnly = 0x02,
    ReadWrite = ReadOnly | WriteOnly,
};

// ============================================================
// PropertyInfo — 描述单个属性
// ============================================================

struct CORE_API PropertyInfo {
    std::string name;               // 属性名
    TypeInfo typeInfo;              // 属性类型
    size_t offset = 0;              // 成员偏移量（offsetof）
    PropertyAccess access = PropertyAccess::ReadWrite;
};

// ============================================================
// TypeRegistry — 全局类型注册表
// ============================================================

class CORE_API TypeRegistry {
public:
    /// Meyer's Singleton
    static TypeRegistry& instance() {
        static TypeRegistry inst;
        return inst;
    }

    // --- 注册 ---

    /// 注册一个类
    void registerClass(std::string_view name,
                       TypeInfo typeInfo,
                       const ClassInfo* baseInfo = nullptr,
                       size_t size = 0,
                       bool isAbstract = false);

    /// 向已注册的类添加属性
    void registerProperty(std::string_view className, PropertyInfo prop);

    // --- 查询 ---

    /// 按类名查询
    const ClassInfo* findClass(std::string_view name) const;

    /// 按 TypeInfo 查询
    const ClassInfo* findClass(const TypeInfo& typeInfo) const;

    /// 按 type_index 查询
    const ClassInfo* findClass(std::type_index idx) const;

    /// 获取类的属性列表
    const std::vector<PropertyInfo>* findProperties(std::string_view className) const;

    /// 获取所有已注册的类名
    std::vector<std::string> registeredClasses() const;

private:
    TypeRegistry() = default;
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    mutable std::mutex m_mutex;

    // 按类名索引
    struct ClassEntry {
        std::unique_ptr<ClassInfo> info;
        std::vector<PropertyInfo> properties;
    };
    std::unordered_map<std::string, ClassEntry> m_classesByName;

    // 按 type_index 索引（指向 m_classesByName 中的数据，不拥有）
    std::unordered_map<std::type_index, ClassInfo*> m_classesByType;
};

} // namespace mulan::core
