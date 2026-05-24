/**
 * @file Macro.h
 * @brief MULANGEO_OBJECT / MULANGEO_PROPERTY 宏定义
 *
 * 使用方式：
 *
 *   class MyGeometry : public mulan::core::Object {
 *   public:
 *       MULANGEO_OBJECT(MyGeometry, Object)
 *       // ... 自定义字段和方法 ...
 *
 *       void serialize(mulan::core::OutputArchive& ar) const override { ... }
 *       void serialize(mulan::core::InputArchive& ar) override { ... }
 *   };
 *
 * MULANGEO_OBJECT 展开：
 *   1. static ClassInfo 静态成员 + classInfo() 实现
 *   2. create() 工厂方法
 *   3. inline static 变量自动注册到 ObjectFactory + TypeRegistry
 *     （C++17 inline static 消除两段式，无需在 .cpp 中额外调用）
 */
#pragma once

#include "Object.h"
#include "TypeRegistry.h"

// ============================================================
// MULANGEO_OBJECT — 派生类宏
// ============================================================

#define MULANGEO_OBJECT(ClassName, BaseClassName)                                         \
public:                                                                                   \
    static const ::mulan::core::ClassInfo& staticClassInfo() {                         \
        static const ::mulan::core::ClassInfo s_info(                                  \
            #ClassName,                                                                   \
            ::mulan::core::TypeInfo::of<ClassName>(),                                  \
            &BaseClassName::staticClassInfo(),                                             \
            sizeof(ClassName),                                                             \
            false);                                                                        \
        return s_info;                                                                     \
    }                                                                                     \
                                                                                           \
    const ::mulan::core::ClassInfo& classInfo() const noexcept override {              \
        return staticClassInfo();                                                          \
    }                                                                                     \
                                                                                           \
    std::unique_ptr<::mulan::core::Object> create() const override {                   \
        return std::make_unique<ClassName>();                                              \
    }                                                                                     \
                                                                                           \
    static std::unique_ptr<::mulan::core::Object> createStatic() {                     \
        return std::make_unique<ClassName>();                                              \
    }                                                                                     \
private:                                                                                  \
    static inline bool _registered_##ClassName = [] {                                     \
        ::mulan::core::ObjectFactory::instance().registerType(                         \
            #ClassName, &ClassName::createStatic);                                        \
        ::mulan::core::TypeRegistry::instance().registerClass(                         \
            #ClassName,                                                                   \
            ::mulan::core::TypeInfo::of<ClassName>(),                                  \
            &BaseClassName::staticClassInfo(),                                             \
            sizeof(ClassName),                                                             \
            false);                                                                        \
        return true;                                                                      \
    }();                                                                                  \
public:

// ============================================================
// MULANGEO_PROPERTY — 属性注册宏
//
// 使用方式（在 .cpp 文件中）：
//   MULANGEO_PROPERTY(MyGeometry, "width", width_, mulan::core::PropertyAccess::ReadWrite)
// ============================================================

#define MULANGEO_PROPERTY(ClassName, PropName, MemberPtr, Access)                         \
    do {                                                                                  \
        ::mulan::core::PropertyInfo prop;                                              \
        prop.name = (PropName);                                                           \
        prop.offset = offsetof(ClassName, MemberPtr);                                     \
        prop.access = (Access);                                                           \
        ::mulan::core::TypeRegistry::instance().registerProperty(#ClassName,           \
                                                                      std::move(prop));   \
    } while (0)
