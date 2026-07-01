/**
 * @file object.h
 * @brief Object 多态基类 + ObjectFactory 全局单例工厂
 *
 * Object 是所有需要按基类指针进行多态序列化的实体类型的统一基础。
 * 值类型（EntityId、Mat4 等）不继承 Object，而是通过 Serializer<T> 特化接入。
 */
#pragma once

#include "../core_export.h"
#include "../serialization/archive.h"
#include "../serialization/archive_error.h"
#include "type_info.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mulan::core {

// ============================================================
// ClassInfo — 类描述信息（用于继承判断、属性面板等）
// ============================================================

class CORE_API ClassInfo {
public:
    ClassInfo(std::string_view name, TypeInfo typeInfo, const ClassInfo* baseInfo = nullptr,
              size_t size = 0, bool isAbstract = false)
        : name_(name)
        , type_info_(std::move(typeInfo))
        , base_info_(baseInfo)
        , size_(size)
        , is_abstract_(isAbstract) {}

    const std::string& name() const { return name_; }
    const TypeInfo& typeInfo() const { return type_info_; }
    const ClassInfo* baseInfo() const { return base_info_; }
    size_t size() const { return size_; }
    bool isAbstract() const { return is_abstract_; }

    /// 判断 this 是否从 other 派生（沿基类链向上搜索）
    bool isDerivedFrom(const ClassInfo* other) const {
        const ClassInfo* current = this;
        while (current) {
            if (current == other) return true;
            current = current->base_info_;
        }
        return false;
    }

private:
    std::string name_;
    TypeInfo type_info_;
    const ClassInfo* base_info_;
    size_t size_;
    bool is_abstract_;
};

// ============================================================
// Object — 多态实体基类
// ============================================================

class CORE_API Object {
public:
    virtual ~Object() = default;

    /// 写方向：序列化到 OutputArchive
    virtual void serialize(OutputArchive& ar) const = 0;

    /// 读方向：从 InputArchive 反序列化（重载区分方向）
    virtual void serialize(InputArchive& ar) = 0;

    /// 返回类型的 ClassInfo（由 MULANGEO_OBJECT 宏实现）
    virtual const ClassInfo& classInfo() const noexcept = 0;

    /// 便捷方法：获取类型名
    const std::string& typeName() const noexcept {
        return classInfo().name();
    }

    /// 创建自身类型的默认实例（由 MULANGEO_OBJECT 宏实现）
    virtual std::unique_ptr<Object> create() const = 0;

    /// Object 根类的静态 ClassInfo（供 MULANGEO_OBJECT 宏引用基类的 staticClassInfo）
    static const ClassInfo& staticClassInfo() {
        static const ClassInfo s_info("Object", TypeInfo::of<Object>(), nullptr, sizeof(Object), true);
        return s_info;
    }
};

// ============================================================
// ObjectFactory — 全局单例工厂
// ============================================================

class CORE_API ObjectFactory {
public:
    /// Meyer's Singleton：函数内 static，避免 SIOF
    static ObjectFactory& instance() {
        static ObjectFactory inst;
        return inst;
    }

    using Creator = std::unique_ptr<Object>(*)();

    /// 注册类型（线程安全，支持动态插件加载）
    void registerType(std::string_view typeName, Creator fn);

    /// 按类型名创建实例
    std::unique_ptr<Object> create(std::string_view typeName) const;

    /// 查询类型是否已注册
    bool isRegistered(std::string_view typeName) const;

    /// 获取所有已注册的类型名
    std::vector<std::string> registeredTypes() const;

private:
    ObjectFactory() = default;
    ObjectFactory(const ObjectFactory&) = delete;
    ObjectFactory& operator=(const ObjectFactory&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Creator> creators_;
};


} // namespace mulan::core

// ============================================================
// Serializer<std::unique_ptr<Object>> 声明（DLL 导出）
// 实现在 Object.cpp 中
// ============================================================

#include "../serialization/serializer.h"

namespace mulan::core {

template<>
struct CORE_API Serializer<std::unique_ptr<Object>> {
    static void write(OutputArchive& ar, const std::unique_ptr<Object>& obj);
    static ArchiveResult read(InputArchive& ar, std::unique_ptr<Object>& out);
};

} // namespace mulan::core
