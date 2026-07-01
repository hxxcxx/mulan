#include "type_registry.h"

#include <algorithm>
#include <memory>

namespace mulan::core {

void TypeRegistry::registerClass(std::string_view name,
                                  TypeInfo typeInfo,
                                  const ClassInfo* baseInfo,
                                  size_t size,
                                  bool isAbstract) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key(name);

    auto& entry = classes_by_name_[key];
    if (entry.info) return;  // 已注册，不重复注册

    entry.info = std::make_unique<ClassInfo>(name, std::move(typeInfo), baseInfo, size, isAbstract);

    // 同步到 type_index 索引
    classes_by_type_[entry.info->typeInfo().typeIndex()] = entry.info.get();
}

void TypeRegistry::registerProperty(std::string_view className, PropertyInfo prop) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = classes_by_name_.find(std::string(className));
    if (it == classes_by_name_.end()) return;

    it->second.properties.push_back(std::move(prop));
}

const ClassInfo* TypeRegistry::findClass(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = classes_by_name_.find(std::string(name));
    if (it == classes_by_name_.end()) return nullptr;
    return it->second.info.get();
}

const ClassInfo* TypeRegistry::findClass(const TypeInfo& typeInfo) const {
    return findClass(typeInfo.typeIndex());
}

const ClassInfo* TypeRegistry::findClass(std::type_index idx) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = classes_by_type_.find(idx);
    if (it == classes_by_type_.end()) return nullptr;
    return it->second;
}

const std::vector<PropertyInfo>* TypeRegistry::findProperties(std::string_view className) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = classes_by_name_.find(std::string(className));
    if (it == classes_by_name_.end()) return nullptr;
    return &it->second.properties;
}

std::vector<std::string> TypeRegistry::registeredClasses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(classes_by_name_.size());
    for (const auto& [name, _] : classes_by_name_) {
        names.push_back(name);
    }
    return names;
}

} // namespace mulan::core
