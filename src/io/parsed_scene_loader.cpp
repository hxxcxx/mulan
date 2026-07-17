#include "parsed_scene_loader.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/core/image/image.h>
#include <mulan/io/document.h>
#include <mulan/scene/scene.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>

namespace mulan::io {

namespace {

std::vector<std::byte> readBinaryFile(const std::string& path) {
    if (path.empty())
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size <= 0)
        return {};
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file)
        return {};
    return bytes;
}

}  // namespace

ParsedSceneLoader::ParsedSceneLoader(Document& document) : document_(document) {
}

ImportResult ParsedSceneLoader::load(ParsedScene&& scene, const ImportOptions& options) {
    report_ = {};
    ImportResult result;
    if (scene.nodes.size() > options.maxNodeCount) {
        result.report.warnings.push_back("Scene node count exceeds configured import limit");
        return result;
    }

    loadTextures(scene);
    loadMaterials(scene);
    loadMeshes(scene);
    loadBreps(scene);

    result.report = report_;
    nodeEntities_.resize(scene.nodes.size());

    std::vector<std::vector<size_t>> children(scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        if (scene.nodes[i].parent < scene.nodes.size())
            children[scene.nodes[i].parent].push_back(i);
    }
    std::vector<uint8_t> visitState(scene.nodes.size(), 0);

    const math::Mat4 unitScaleMat = math::Mat4::scale(math::Vec3(options.unitScale > 0.0 ? options.unitScale : 1.0));

    // 根节点的 worldTransform 是单位变换的"父世界";loadNode 内部把 unitScale
    // 乘进根节点的 local(使其 world = unitScale * local)。
    for (size_t rootIdx : scene.rootNodes) {
        if (rootIdx >= scene.nodes.size())
            continue;
        loadNode(rootIdx, scene, scene::EntityId::invalid(), math::Mat4{ 1.0 }, unitScaleMat, options, children,
                 visitState, 0, result);
    }

    return result;
}

void ParsedSceneLoader::loadTextures(ParsedScene& scene) {
    auto* library = document_.assets();
    textureIds_.assign(scene.textures.size(), asset::AssetId::invalid());
    if (!library)
        return;

    for (size_t i = 0; i < scene.textures.size(); ++i) {
        auto& desc = scene.textures[i];
        auto* texture = library->create<asset::TextureAsset>(std::move(desc.name), std::move(desc.sourcePath));
        if (!texture)
            continue;

        std::vector<std::byte> encodedBytes =
                desc.data.empty() ? readBinaryFile(texture->sourcePath()) : std::move(desc.data);
        std::shared_ptr<core::Image> image;
        std::string decodeError;
        core::ImageDecodeOptions decodeOptions;
        decodeOptions.outputOrigin = core::ImageOrigin::TopLeft;
        decodeOptions.channels = core::ImageChannelLayout::RGBA;
        if (!encodedBytes.empty()) {
            auto decoded = core::Image::loadFromMemory(std::span<const std::byte>(encodedBytes), decodeOptions);
            if (decoded) {
                image = std::move(decoded).value();
            } else {
                decodeError = decoded.error().message;
            }
        }

        if (image && image->valid()) {
            texture->setImage(std::move(image));
        } else if (!texture->sourcePath().empty() || !encodedBytes.empty()) {
            const std::string& source = texture->sourcePath().empty() ? texture->name() : texture->sourcePath();
            report_.warnings.push_back("Failed to decode texture image: " + source +
                                       (decodeError.empty() ? std::string{} : " (" + decodeError + ")"));
        }

        if (!encodedBytes.empty()) {
            texture->setEmbeddedBytes(std::move(encodedBytes));
            texture->setMimeType(std::move(desc.mimeType));
        } else if (!texture->sourcePath().empty()) {
            report_.warnings.push_back("Failed to read texture bytes: " + texture->sourcePath());
        }
        if (desc.width > 0 && desc.height > 0)
            texture->setSize(desc.width, desc.height);

        textureIds_[i] = texture->id();
        ++report_.textureCount;
    }
}

void ParsedSceneLoader::loadMaterials(ParsedScene& scene) {
    auto* library = document_.assets();
    materialIds_.assign(scene.materials.size(), asset::AssetId::invalid());
    if (!library)
        return;

    for (size_t i = 0; i < scene.materials.size(); ++i) {
        auto& desc = scene.materials[i];
        auto* material = library->create<asset::MaterialAsset>(std::move(desc.name));
        if (!material)
            continue;

        material->setBaseColorFactor(desc.baseColorFactor);
        material->setRoughness(desc.roughness);
        material->setMetallic(desc.metallic);
        material->setBaseColorTexture(textureAssetId(desc.baseColorTexture));
        material->setBaseColorTextureSrgb(desc.baseColorTextureSrgb);
        material->setNormalTexture(textureAssetId(desc.normalTexture));
        material->setNormalTextureSrgb(desc.normalTextureSrgb);
        material->setMetallicRoughnessTexture(textureAssetId(desc.metallicRoughnessTexture));
        material->setMetallicRoughnessTextureSrgb(desc.metallicRoughnessTextureSrgb);
        material->setEmissiveTexture(textureAssetId(desc.emissiveTexture));
        material->setEmissiveTextureSrgb(desc.emissiveTextureSrgb);
        material->setEmissiveFactor(desc.emissiveFactor);
        material->setEmissiveStrength(desc.emissiveStrength);
        material->setOcclusionTexture(textureAssetId(desc.occlusionTexture));
        material->setOcclusionTextureSrgb(desc.occlusionTextureSrgb);
        material->setAlphaMode(desc.alphaMode);
        material->setDoubleSided(desc.doubleSided);

        materialIds_[i] = material->id();
        ++report_.materialCount;
    }
}

void ParsedSceneLoader::loadMeshes(ParsedScene& scene) {
    auto* library = document_.assets();
    meshIds_.assign(scene.meshes.size(), asset::AssetId::invalid());
    if (!library)
        return;

    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        auto& parsed = scene.meshes[i];
        if (parsed.primitives.empty())
            continue;

        for (size_t p = 0; p < parsed.primitives.size(); ++p) {
            const size_t matIdx = (p < parsed.materialIndices.size()) ? parsed.materialIndices[p] : SIZE_MAX;
            parsed.primitives[p].material = materialAssetId(matIdx);
        }

        const size_t primitiveCount = parsed.primitives.size();
        auto* mesh = library->create<asset::MeshAsset>(std::move(parsed.name), std::move(parsed.primitives));
        if (!mesh)
            continue;

        meshIds_[i] = mesh->id();
        report_.primitiveCount += primitiveCount;
        ++report_.meshAssetCount;
    }
}

void ParsedSceneLoader::loadBreps(ParsedScene& scene) {
    auto* library = document_.assets();
    brepIds_.assign(scene.breps.size(), asset::AssetId::invalid());
    if (!library)
        return;

    for (size_t i = 0; i < scene.breps.size(); ++i) {
        auto& parsed = scene.breps[i];
        auto* brep = library->create<asset::BRepAsset>(std::move(parsed.name), std::move(parsed.shape));
        if (!brep)
            continue;
        brepIds_[i] = brep->id();
        ++report_.brepAssetCount;
    }
}

void ParsedSceneLoader::loadNode(size_t nodeIndex, const ParsedScene& scene, scene::EntityId parentEntity,
                                 const math::Mat4& parentWorld, const math::Mat4& rootUnitScale,
                                 const ImportOptions& options, const std::vector<std::vector<size_t>>& children,
                                 std::vector<uint8_t>& visitState, size_t depth, ImportResult& result) {
    if (nodeIndex >= scene.nodes.size())
        return;
    if (depth > options.maxNodeDepth) {
        result.report.warnings.push_back("Scene node depth exceeds configured import limit");
        return;
    }
    if (visitState[nodeIndex] == 1) {
        result.report.warnings.push_back("Cycle detected in imported scene node hierarchy");
        return;
    }
    if (visitState[nodeIndex] == 2)
        return;
    visitState[nodeIndex] = 1;

    const auto& node = scene.nodes[nodeIndex];
    auto* scn = document_.scene();
    if (!scn)
        return;

    // 根节点(无父)的 local 烤入 unitScale,使其 world = unitScale * local;
    // 非根节点 local 不变。
    const bool isRoot = !parentEntity;
    const math::Mat4 effectiveLocal = isRoot ? (rootUnitScale * node.localTransform) : node.localTransform;
    const math::Mat4 nodeWorld = parentWorld * effectiveLocal;

    scene::EntityId entity = scn->createEntity(node.name);
    if (parentEntity)
        scn->setParent(entity, parentEntity);
    scn->setLocalTransform(entity, effectiveLocal);

    // 资源绑定
    if (node.hasMesh() && node.meshIndex < meshIds_.size() && meshIds_[node.meshIndex]) {
        const asset::AssetId meshId = meshIds_[node.meshIndex];
        scn->setGeometry(entity, meshId);
        // 材质槽:从 MeshAsset 的 primitive 取出。
        if (auto* library = document_.assets()) {
            if (auto* asset = library->asset(meshId)) {
                if (const auto* meshAsset = dynamic_cast<const asset::MeshAsset*>(asset)) {
                    std::vector<asset::AssetId> slots;
                    for (const auto& prim : meshAsset->primitives())
                        slots.push_back(prim.material);
                    scn->setMaterialSlots(entity, std::move(slots));
                }
            }
        }
    }

    if (node.hasBRep() && node.brepIndex < brepIds_.size() && brepIds_[node.brepIndex]) {
        const asset::AssetId brepId = brepIds_[node.brepIndex];
        scn->setGeometry(entity, brepId);
    }

    if (node.hasLight() && node.lightIndex < scene.lights.size()) {
        const auto& pl = scene.lights[node.lightIndex];
        scene::LightComponent lc;
        lc.kind = pl.kind;
        lc.color = pl.color;
        lc.intensity = pl.intensity;
        // range 与节点位置使用同一文档长度单位；全局导入缩放不能只缩位置而遗漏衰减范围。
        lc.range = pl.range * (options.unitScale > 0.0 ? options.unitScale : 1.0);
        lc.innerConeAngle = pl.innerConeAngle;
        lc.outerConeAngle = pl.outerConeAngle;
        scn->setLight(entity, lc);
    }

    nodeEntities_[nodeIndex] = entity;
    result.entities.push_back(entity);
    ++result.report.entityCount;

    // 递归子节点:子的 parentWorld = 本节点 world
    for (size_t child : children[nodeIndex])
        loadNode(child, scene, entity, nodeWorld, rootUnitScale, options, children, visitState, depth + 1, result);
    visitState[nodeIndex] = 2;
}

void ParsedSceneLoader::loadNodes(const ParsedScene& scene, const ImportOptions& options) {
    (void) scene;
    (void) options;
}

asset::AssetId ParsedSceneLoader::textureAssetId(size_t parsedIndex) const {
    return (parsedIndex < textureIds_.size()) ? textureIds_[parsedIndex] : asset::AssetId::invalid();
}

asset::AssetId ParsedSceneLoader::materialAssetId(size_t parsedIndex) const {
    if (parsedIndex == SIZE_MAX)
        return asset::AssetId::invalid();
    return (parsedIndex < materialIds_.size()) ? materialIds_[parsedIndex] : asset::AssetId::invalid();
}

asset::AssetId ParsedSceneLoader::meshAssetId(size_t parsedIndex) const {
    return (parsedIndex < meshIds_.size()) ? meshIds_[parsedIndex] : asset::AssetId::invalid();
}

asset::AssetId ParsedSceneLoader::brepAssetId(size_t parsedIndex) const {
    return (parsedIndex < brepIds_.size()) ? brepIds_[parsedIndex] : asset::AssetId::invalid();
}

}  // namespace mulan::io
