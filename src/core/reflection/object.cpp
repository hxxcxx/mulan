#include "object.h"
#include "../serialization/serializer.h"

#include <mutex>

namespace mulan::core {

// ============================================================
// ObjectFactory 实现
// ============================================================

void ObjectFactory::registerType(std::string_view typeName, Creator fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    creators_[std::string(typeName)] = fn;
}

std::unique_ptr<Object> ObjectFactory::create(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = creators_.find(std::string(typeName));
    if (it == creators_.end()) return nullptr;
    return it->second();
}

bool ObjectFactory::isRegistered(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return creators_.find(std::string(typeName)) != creators_.end();
}

std::vector<std::string> ObjectFactory::registeredTypes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& [name, _] : creators_) {
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
        return std::unexpected(
            ArchiveError::make(ArchiveError::Code::MissingKey,
                               "Unknown object type: " + typeName));
    }

    obj->serialize(ar);  // 虚函数分发到具体类型的读方向
    if (ar.hasError()) {
        return std::unexpected(
            ArchiveError::make(ArchiveError::Code::CorruptedData,
                               ar.errorMessage()));
    }

    out = std::move(obj);
    return {};
}

} // namespace mulan::core
