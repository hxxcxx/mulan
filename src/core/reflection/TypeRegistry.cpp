/**
 * @file TypeRegistry.cpp
 * @brief TypeRegistry 全局类型注册表实现
 */

#include "TypeRegistry.h"

#include <algorithm>
#include <memory>

namespace mulan::core {

void TypeRegistry::registerClass(std::string_view name,
                                  TypeInfo typeInfo,
                                  const ClassInfo* baseInfo,
                                  size_t size,
                                  bool isAbstract) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key(name);

    auto& entry = m_classesByName[key];
    if (entry.info) return;  // 已注册，不重复注册

    entry.info = std::make_unique<ClassInfo>(name, std::move(typeInfo), baseInfo, size, isAbstract);

    // 同步到 type_index 索引
    m_classesByType[entry.info->typeInfo().typeIndex()] = entry.info.get();
}

void TypeRegistry::registerProperty(std::string_view className, PropertyInfo prop) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_classesByName.find(std::string(className));
    if (it == m_classesByName.end()) return;

    it->second.properties.push_back(std::move(prop));
}

const ClassInfo* TypeRegistry::findClass(std::string_view name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_classesByName.find(std::string(name));
    if (it == m_classesByName.end()) return nullptr;
    return it->second.info.get();
}

const ClassInfo* TypeRegistry::findClass(const TypeInfo& typeInfo) const {
    return findClass(typeInfo.typeIndex());
}

const ClassInfo* TypeRegistry::findClass(std::type_index idx) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_classesByType.find(idx);
    if (it == m_classesByType.end()) return nullptr;
    return it->second;
}

const std::vector<PropertyInfo>* TypeRegistry::findProperties(std::string_view className) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_classesByName.find(std::string(className));
    if (it == m_classesByName.end()) return nullptr;
    return &it->second.properties;
}

std::vector<std::string> TypeRegistry::registeredClasses() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_classesByName.size());
    for (const auto& [name, _] : m_classesByName) {
        names.push_back(name);
    }
    return names;
}

} // namespace mulan::core
