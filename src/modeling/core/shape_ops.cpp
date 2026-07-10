#include "shape_ops.h"

#include <algorithm>
#include <cctype>

namespace mulan::modeling {

namespace {

std::string normalizedBackendName(std::string backend) {
    std::transform(backend.begin(), backend.end(), backend.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return backend;
}

}  // namespace

ShapeOpsRegistry& ShapeOpsRegistry::instance() {
    static ShapeOpsRegistry registry;
    return registry;
}

void ShapeOpsRegistry::registerOps(std::string backend, std::unique_ptr<IShapeOps> ops) {
    backend = normalizedBackendName(std::move(backend));
    if (backend.empty() || !ops)
        return;
    backends_[std::move(backend)] = std::move(ops);
}

void ShapeOpsRegistry::selectBackend(std::string backend) {
    backend = normalizedBackendName(std::move(backend));
    if (!backend.empty())
        selectedBackend_ = std::move(backend);
}

std::string ShapeOpsRegistry::selectedBackend() const {
    return selectedBackend_;
}

std::vector<std::string> ShapeOpsRegistry::availableBackends() const {
    std::vector<std::string> names;
    names.reserve(backends_.size());
    for (const auto& [name, _] : backends_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

IShapeOps* ShapeOpsRegistry::ops() const {
    const auto it = backends_.find(selectedBackend_);
    return it == backends_.end() ? nullptr : it->second.get();
}

}  // namespace mulan::modeling
