/**
 * @file MaterialSerializer.cpp
 * @brief 材质序列化/反序列化实现
 */

#include "MaterialSerializer.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace MulanGeo::engine {

using json = nlohmann::json;

// ============================================================
// 序列化
// ============================================================

std::string MaterialSerializer::toJson(const Material& m) {
    json j;
    j["name"]             = m.name.empty() ? "Unnamed" : m.name;
    j["type"]             = materialTypeToString(m.type);
    j["alphaMode"]        = alphaModeToString(m.alphaMode);
    j["baseColor"]        = {m.baseColor.x, m.baseColor.y, m.baseColor.z};
    j["alpha"]            = m.alpha;
    j["metallic"]         = m.metallic;
    j["roughness"]        = m.roughness;
    j["ao"]               = m.ao;
    j["specular"]         = {m.specular.x, m.specular.y, m.specular.z};
    j["shininess"]        = m.shininess;
    j["emissive"]         = {m.emissive.x, m.emissive.y, m.emissive.z};
    j["emissiveStrength"] = m.emissiveStrength;
    j["alphaCutoff"]      = m.alphaCutoff;
    j["doubleSided"]      = m.doubleSided;

    // 纹理路径（仅非空的槽位）
    json texObj = json::object();
    for (size_t i = 0; i < static_cast<size_t>(TextureSlot::Count); ++i) {
        if (!m.texturePaths[i].empty()) {
            texObj[textureSlotName(static_cast<TextureSlot>(i))] = m.texturePaths[i];
        }
    }
    j["textures"] = texObj;

    return j.dump(2);
}

// ============================================================
// 反序列化
// ============================================================

SerializeResult MaterialSerializer::fromJson(const std::string& jsonStr, Material& out) {
    SerializeResult result;
    try {
        auto j = json::parse(jsonStr);
        Material m;

        if (j.contains("name"))             m.name             = j["name"].get<std::string>();
        if (j.contains("type"))             m.type             = materialTypeFromString(j["type"].get<std::string>());
        if (j.contains("alphaMode"))        m.alphaMode        = alphaModeFromString(j["alphaMode"].get<std::string>());
        if (j.contains("alpha"))            m.alpha            = j["alpha"].get<double>();
        if (j.contains("metallic"))         m.metallic         = j["metallic"].get<double>();
        if (j.contains("roughness"))        m.roughness        = j["roughness"].get<double>();
        if (j.contains("ao"))               m.ao               = j["ao"].get<double>();
        if (j.contains("shininess"))        m.shininess        = j["shininess"].get<double>();
        if (j.contains("emissiveStrength")) m.emissiveStrength = j["emissiveStrength"].get<double>();
        if (j.contains("alphaCutoff"))      m.alphaCutoff      = j["alphaCutoff"].get<double>();
        if (j.contains("doubleSided"))      m.doubleSided      = j["doubleSided"].get<bool>();

        if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() >= 3)
            m.baseColor = Vec3(j["baseColor"][0], j["baseColor"][1], j["baseColor"][2]);

        if (j.contains("specular") && j["specular"].is_array() && j["specular"].size() >= 3)
            m.specular = Vec3(j["specular"][0], j["specular"][1], j["specular"][2]);

        if (j.contains("emissive") && j["emissive"].is_array() && j["emissive"].size() >= 3)
            m.emissive = Vec3(j["emissive"][0], j["emissive"][1], j["emissive"][2]);

        if (j.contains("textures") && j["textures"].is_object()) {
            for (auto& [key, val] : j["textures"].items()) {
                for (size_t i = 0; i < static_cast<size_t>(TextureSlot::Count); ++i) {
                    if (key == textureSlotName(static_cast<TextureSlot>(i))) {
                        m.texturePaths[i] = val.get<std::string>();
                        break;
                    }
                }
            }
        }

        out = std::move(m);
        result.success = true;
    } catch (const json::exception& e) {
        result.error = e.what();
    }
    return result;
}

// ============================================================
// 文件 I/O
// ============================================================

SerializeResult MaterialSerializer::saveToFile(const std::string& path, const Material& material) {
    SerializeResult result;
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        result.error = "Cannot open file for writing: " + path;
        return result;
    }
    ofs << toJson(material);
    result.success = true;
    return result;
}

SerializeResult MaterialSerializer::loadFromFile(const std::string& path, Material& outMaterial) {
    SerializeResult result;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        result.error = "Cannot open file for reading: " + path;
        return result;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return fromJson(content, outMaterial);
}

// ============================================================
// 批量
// ============================================================

SerializeResult MaterialSerializer::saveCollection(const std::string& path,
                                                     const std::vector<Material>& materials) {
    SerializeResult result;
    json arr = json::array();
    for (const auto& m : materials) {
        arr.push_back(json::parse(toJson(m)));
    }
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        result.error = "Cannot open file for writing: " + path;
        return result;
    }
    ofs << arr.dump(2);
    result.success = true;
    return result;
}

SerializeResult MaterialSerializer::loadCollection(const std::string& path,
                                                     std::vector<Material>& outMaterials) {
    SerializeResult result;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        result.error = "Cannot open file for reading: " + path;
        return result;
    }
    try {
        auto arr = json::parse(ifs);
        if (!arr.is_array()) {
            result.error = "Expected JSON array at top level";
            return result;
        }
        outMaterials.clear();
        for (const auto& elem : arr) {
            Material m;
            auto r = fromJson(elem.dump(), m);
            if (r.success) {
                outMaterials.push_back(std::move(m));
            }
        }
        result.success = !outMaterials.empty();
        if (!result.success) result.error = "No valid materials found in file";
    } catch (const json::exception& e) {
        result.error = e.what();
    }
    return result;
}

} // namespace MulanGeo::Engine
