#include "gltf_importer.h"
#include "mesh_import_builder.h"

#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/asset_library.h>
#include <mulan/io/document.h>
#include <mulan/scene/scene.h>
#include <mulan/math/linalg/transform.h>
#include <mulan/engine/geometry/mesh.h>
#include <mulan/engine/vertex/vertex_layout.h>
#include <mulan/engine/vertex/vertex_buffer.h>
#include <mulan/core/result/error.h>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>

namespace mulan::io {
namespace {

using namespace fastgltf;

/// glTF 文件所在目录
std::string fileDirectory(const std::string& filePath) {
    return std::filesystem::path(filePath).parent_path().string();
}

/// 读取索引数据
std::vector<uint32_t> readIndices(const Asset& asset, const Accessor& accessor) {
    std::vector<uint32_t> result(accessor.count);
    iterateAccessorWithIndex<uint32_t>(
        asset, accessor,
        [&](uint32_t idx, size_t i) { result[i] = idx; });
    return result;
}

/// 读取 fvec3 数据到 mulan math::FVec3
std::vector<math::FVec3> readPositions(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec3> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec3>(
        asset, acc,
        [&](fastgltf::math::fvec3 v, size_t i) {
            result[i] = {v[0], v[1], v[2]};
        });
    return result;
}

/// 读取 fvec2 数据到 mulan math::FVec2
std::vector<math::FVec2> readTexcoords(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec2> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec2>(
        asset, acc,
        [&](fastgltf::math::fvec2 v, size_t i) {
            result[i] = {v[0], v[1]};
        });
    return result;
}

/// 导入纹理，返回 {gltfImageIndex → AssetId}
std::map<size_t, asset::AssetId> importTextures(
    const Asset& gltf, MeshImportBuilder& builder, const std::string& sourceDir) {

    std::map<size_t, asset::AssetId> texMap;
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const auto& image = gltf.images[i];
        std::string texPath;

        std::visit(visitor{
            [&](const sources::URI& uri) {
                if (!uri.uri.isLocalPath()) return;
                auto p = std::filesystem::path(uri.uri.path());
                texPath = p.is_relative()
                    ? (std::filesystem::path(sourceDir) / p).string()
                    : p.string();
            },
            [](auto&) {}
        }, image.data);

        ImportedTextureDesc desc;
        desc.name = image.name.empty()
            ? "Texture_" + std::to_string(i) : std::string(image.name);
        desc.sourcePath = texPath;
        desc.srgb = true;

        asset::AssetId texId = builder.createTexture(desc);
        if (texId) texMap[i] = texId;
    }
    return texMap;
}

/// 导入材质，返回 {gltfMaterialIndex → AssetId}
std::map<size_t, asset::AssetId> importMaterials(
    const Asset& gltf, MeshImportBuilder& builder,
    const std::map<size_t, asset::AssetId>& texMap) {

    std::map<size_t, asset::AssetId> matMap;

    auto mapTex = [&](std::optional<size_t> texIdx) -> asset::AssetId {
        if (!texIdx.has_value() || *texIdx >= gltf.textures.size())
            return asset::AssetId::invalid();
        size_t imgIdx = gltf.textures[*texIdx].imageIndex.value_or(0);
        auto it = texMap.find(imgIdx);
        return it != texMap.end() ? it->second : asset::AssetId::invalid();
    };

    for (size_t i = 0; i < gltf.materials.size(); ++i) {
        const auto& mat = gltf.materials[i];
        const auto& pbr = mat.pbrData;

        ImportedMaterialDesc desc;
        desc.name = mat.name.empty()
            ? "Material_" + std::to_string(i) : std::string(mat.name);

        desc.baseColorFactor = {
            pbr.baseColorFactor[0], pbr.baseColorFactor[1],
            pbr.baseColorFactor[2], pbr.baseColorFactor[3]};
        desc.roughness = static_cast<double>(pbr.roughnessFactor);
        desc.metallic  = static_cast<double>(pbr.metallicFactor);

        if (pbr.baseColorTexture.has_value())
            desc.baseColorTexture = mapTex(pbr.baseColorTexture->textureIndex);
        if (pbr.metallicRoughnessTexture.has_value())
            desc.metallicRoughnessTexture = mapTex(pbr.metallicRoughnessTexture->textureIndex);
        if (mat.normalTexture.has_value())
            desc.normalTexture = mapTex(mat.normalTexture->textureIndex);
        if (mat.emissiveTexture.has_value())
            desc.emissiveTexture = mapTex(mat.emissiveTexture->textureIndex);
        if (mat.occlusionTexture.has_value())
            desc.occlusionTexture = mapTex(mat.occlusionTexture->textureIndex);

        desc.emissiveFactor = {mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]};

        desc.alphaMode   = static_cast<asset::AlphaMode>(mat.alphaMode);
        desc.doubleSided = mat.doubleSided;

        asset::AssetId matId = builder.createMaterial(desc);
        if (matId) matMap[i] = matId;
    }

    if (matMap.empty()) {
        ImportedMaterialDesc def;
        def.name = "DefaultPBR";
        matMap[0] = builder.createMaterial(def);
    }
    return matMap;
}

/// 从 glTF primitive 构建 StandardMeshSource
/// 返回的 source 中 span 指向函数内的静态/局部数据——调用方必须在 source 生命周期内保持数据有效。
struct PrimitiveGeomData {
    std::vector<math::FVec3> positions;
    std::vector<math::FVec3> normals;
    std::vector<math::FVec2> texcoords;
    std::vector<uint32_t> indices;
};

PrimitiveGeomData extractPrimitiveData(const Asset& gltf, const Primitive& prim) {
    PrimitiveGeomData geom;

    auto* posAttr = prim.findAttribute("POSITION");
    if (posAttr) {
        auto& acc = gltf.accessors[posAttr->accessorIndex];
        geom.positions = readPositions(gltf, acc);
    }

    auto* nrmAttr = prim.findAttribute("NORMAL");
    if (nrmAttr) {
        auto& acc = gltf.accessors[nrmAttr->accessorIndex];
        geom.normals = readPositions(gltf, acc); // NORMAL 也是 fvec3
    }

    auto* uvAttr = prim.findAttribute("TEXCOORD_0");
    if (uvAttr) {
        auto& acc = gltf.accessors[uvAttr->accessorIndex];
        geom.texcoords = readTexcoords(gltf, acc);
    }

    if (prim.indicesAccessor.has_value()) {
        auto& acc = gltf.accessors[prim.indicesAccessor.value()];
        geom.indices = readIndices(gltf, acc);
    }

    return geom;
}

} // namespace

// ============================================================
// GltfImporter
// ============================================================

std::expected<ImportResult, core::Error>
GltfImporter::import(const std::string& path,
                     mulan::io::Document& doc,
                     const ImportOptions& options) {
    ImportResult result{};
    const std::string sourceDir = fileDirectory(path);

    // --- 1. 加载 glTF 文件 ---
    auto fileStream = GltfFileStream(path);
    if (!fileStream.isOpen()) {
        return std::unexpected(core::Error::make(
            core::ErrorCode::InvalidArg,
            "Failed to open glTF file: " + path));
    }

    Parser parser;
    constexpr auto gltfOpts = Options::LoadExternalBuffers | Options::LoadExternalImages;

    auto assetResult = parser.loadGltf(fileStream,
                                       std::filesystem::path(path).parent_path(),
                                       gltfOpts);

    if (assetResult.error() != Error::None) {
        return std::unexpected(core::Error::make(
            core::ErrorCode::InvalidArg,
            "Failed to parse glTF: " + std::string(getErrorMessage(assetResult.error()))));
    }

    auto& gltf = assetResult.get();

    // --- 2. 导入纹理 ---
    MeshImportBuilder builder(doc);
    auto texMap = importTextures(gltf, builder, sourceDir);
    result.report.textureCount = texMap.size();

    // --- 3. 导入材质 ---
    auto matMap = importMaterials(gltf, builder, texMap);
    result.report.materialCount = matMap.size();

    // --- 4. 收集所有 mesh → MeshAsset ---
    std::vector<asset::AssetId> meshAssets(gltf.meshes.size(), asset::AssetId::invalid());

    for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx) {
        const auto& mesh = gltf.meshes[meshIdx];
        std::string meshName = mesh.name.empty()
            ? "Mesh_" + std::to_string(meshIdx) : std::string(mesh.name);

        std::vector<asset::MeshPrimitive> primitives;

        for (const auto& primitive : mesh.primitives) {
            auto geomData = extractPrimitiveData(gltf, primitive);
            if (geomData.positions.empty()) continue;

            if (geomData.normals.empty())
                geomData.normals.resize(geomData.positions.size(), math::FVec3(0.0f, 0.0f, 1.0f));

            StandardMeshSource src;
            src.positions = std::span(geomData.positions);
            src.normals   = std::span(geomData.normals);
            src.texcoords = std::span(geomData.texcoords);
            src.indices   = std::span(geomData.indices);
            src.topology  = engine::PrimitiveTopology::TriangleList;

            auto engineMesh = buildStandardMesh(src);
            if (engineMesh.empty()) continue;

            asset::MeshPrimitive prim;
            prim.mesh = std::move(engineMesh);
            prim.material = asset::AssetId::invalid();
            if (primitive.materialIndex.has_value()) {
                auto it = matMap.find(*primitive.materialIndex);
                if (it != matMap.end()) prim.material = it->second;
            } else if (!matMap.empty()) {
                prim.material = matMap.begin()->second;
            }
            primitives.push_back(std::move(prim));
        }

        if (!primitives.empty()) {
            auto* assetLib = doc.assets();
            auto* meshAsset = assetLib->create<asset::MeshAsset>(meshName, std::move(primitives));
            if (meshAsset) meshAssets[meshIdx] = meshAsset->id();
            result.report.primitiveCount += primitives.size();
        }
    }

    result.report.meshAssetCount = gltf.meshes.size();

    // --- 5. 遍历 node 树，创建实体层级 ---
    auto* scene = doc.scene();
    if (!scene) return result;

    // TRS → Mat4（使用项目已有的 math::Transform3）
    auto nodeTransform = [](const fastgltf::Node& node) -> math::Mat4 {
        math::Mat4 mat;
        std::visit(fastgltf::visitor{
            [&](const fastgltf::math::fmat4x4& m) {
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        mat[c][r] = static_cast<double>(m[c][r]);
            },
            [&](const fastgltf::TRS& trs) {
                math::Transform3 t;
                t.translation = {trs.translation[0], trs.translation[1], trs.translation[2]};
                t.rotation    = math::Quat(trs.rotation[0], trs.rotation[1],
                                           trs.rotation[2], trs.rotation[3]);
                t.scale       = {trs.scale[0], trs.scale[1], trs.scale[2]};
                mat = t.toMatrix();
            }
        }, node.transform);
        return mat;
    };

    std::function<void(size_t, scene::EntityId)> importNode;
    importNode = [&](size_t nodeIdx, scene::EntityId parent) {
        const auto& node = gltf.nodes[nodeIdx];

        scene::EntityId entity = scene->createEntity(
            node.name.empty() ? "Node_" + std::to_string(nodeIdx) : std::string(node.name));

        if (parent) scene->setParent(entity, parent);

        scene->setLocalTransform(entity, nodeTransform(node));

        if (node.meshIndex.has_value() && *node.meshIndex < meshAssets.size()) {
            auto meshId = meshAssets[*node.meshIndex];
            if (meshId) {
                scene->setGeometry(entity, meshId);
                const auto* meshAsset = dynamic_cast<const asset::MeshAsset*>(
                    doc.assets()->asset(meshId));
                if (meshAsset) {
                    std::vector<asset::AssetId> slots;
                    for (auto& prim : meshAsset->primitives())
                        slots.push_back(prim.material);
                    scene->setMaterialSlots(entity, std::move(slots));
                }
            }
        }

        result.entities.push_back(entity);
        ++result.report.entityCount;

        for (auto childIdx : node.children)
            importNode(childIdx, entity);
    };

    // 从默认 scene 的根节点开始遍历
    if (gltf.defaultScene.has_value()) {
        const auto& sceneNode = gltf.scenes[*gltf.defaultScene];
        for (auto rootIdx : sceneNode.nodeIndices)
            importNode(rootIdx, scene::EntityId::invalid());
    } else {
        // 无 scene 定义时，遍历所有根节点（不被任何节点引用为 child）
        std::vector<bool> isChild(gltf.nodes.size(), false);
        for (auto& n : gltf.nodes)
            for (auto c : n.children) isChild[c] = true;
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
            if (!isChild[i]) importNode(i, scene::EntityId::invalid());
    }

    return result;
}

std::vector<std::string> GltfImporter::supportedExtensions() const {
    return {".gltf", ".glb"};
}

std::string GltfImporter::name() const {
    return "glTF 2.0 (fastgltf)";
}

} // namespace mulan::io
