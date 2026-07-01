/**
 * @file material_serializer.h
 * @brief 材质序列化/反序列化 — JSON 格式
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "material.h"

#include <string>
#include <vector>

namespace mulan::engine {

struct SerializeResult {
    bool        success = false;
    std::string error;
};

class MaterialSerializer {
public:
    static std::string      toJson(const Material& material);
    static SerializeResult  fromJson(const std::string& json, Material& outMaterial);

    static SerializeResult  saveToFile(const std::string& path, const Material& material);
    static SerializeResult  loadFromFile(const std::string& path, Material& outMaterial);

    static SerializeResult  saveCollection(const std::string& path,
                                            const std::vector<Material>& materials);
    static SerializeResult  loadCollection(const std::string& path,
                                            std::vector<Material>& outMaterials);
};

} // namespace mulan::engine
