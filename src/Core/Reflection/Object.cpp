/**
 * @file Object.cpp
 * @brief ObjectFactory 实现 + Serializer<std::unique_ptr<Object>> 多态序列化
 */

#include "Object.h"
#include "../Serialization/Serializer.h"

#include <mutex>

namespace MulanGeo::core {

// ============================================================
// ObjectFactory 实现
// ============================================================

void ObjectFactory::registerType(std::string_view typeName, Creator fn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_creators[std::string(typeName)] = fn;
}

std::unique_ptr<Object> ObjectFactory::create(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_creators.find(std::string(typeName));
    if (it == m_creators.end()) return nullptr;
    return it->second();
}

bool ObjectFactory::isRegistered(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_creators.find(std::string(typeName)) != m_creators.end();
}

std::vector<std::string> ObjectFactory::registeredTypes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_creators.size());
    for (const auto& [name, _] : m_creators) {
        names.push_back(name);
    }
    return names;
}

// ============================================================
// Serializer<std::unique_ptr<Object>> 多态序列化实现
//
// 写模式：typeName + object content
// 读模式：读 typeName → factory create → serialize(InputArchive&)
// ============================================================

void Serializer<std::unique_ptr<Object>>::write(OutputArchive& ar,
                                                  const std::unique_ptr<Object>& obj) {
    if (!obj) {
        ar.write(false);
        return;
    }
    ar.write(true);
    ar.write(std::string_view(obj->typeName()));
    obj->serialize(ar);  // 虚函数分发到具体类型
}

ArchiveResult Serializer<std::unique_ptr<Object>>::read(InputArchive& ar,
                                                         std::unique_ptr<Object>& out) {
    bool hasValue = false;
    auto result = Serializer<bool>::read(ar, hasValue);
    if (!result) return result;

    if (!hasValue) {
        out.reset();
        return {};
    }

    std::string typeName;
    result = Serializer<std::string>::read(ar, typeName);
    if (!result) return result;

    auto obj = ObjectFactory::instance().create(typeName);
    if (!obj) {
        return tl::make_unexpected(
            ArchiveError::make(ArchiveError::Code::MissingKey,
                               "Unknown object type: " + typeName));
    }

    obj->serialize(ar);  // 虚函数分发到具体类型的读方向
    if (ar.hasError()) {
        return tl::make_unexpected(
            ArchiveError::make(ArchiveError::Code::CorruptedData,
                               ar.errorMessage()));
    }

    out = std::move(obj);
    return {};
}

} // namespace MulanGeo::Core
