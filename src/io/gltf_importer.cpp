#include "gltf_importer.h"
#include "import_builder.h"

#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/asset_library.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/scene.h>
#include <mulan/math/linalg/transform.h>
#include <mulan/graphics/mesh.h>
#include <mulan/graphics/vertex/vertex_layout.h>
#include <mulan/graphics/vertex/vertex_buffer.h>
#include <mulan/core/result/error.h>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <map>
#include <variant>

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
    iterateAccessorWithIndex<uint32_t>(asset, accessor, [&](uint32_t idx, size_t i) { result[i] = idx; });
    return result;
}

/// 读取 fvec3 数据到 mulan math::FVec3
std::vector<math::FVec3> readPositions(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec3> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, acc, [&](fastgltf::math::fvec3 v, size_t i) { result[i] = { v[0], v[1], v[2] }; });
    return result;
}

/// 读取 fvec2 数据到 mulan math::FVec2
std::vector<math::FVec2> readTexcoords(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec2> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, acc, [&](fastgltf::math::fvec2 v, size_t i) { result[i] = { v[0], v[1] }; });
    return result;
}

/// fastgltf MimeType → 字符串提示（让 core::Image 自检测，仅作辅助）
std::string mimeTypeString(MimeType mt) {
    switch (mt) {
    case MimeType::JPEG: return "image/jpeg";
    case MimeType::PNG: return "image/png";
    case MimeType::KTX2: return "image/ktx2";
    case MimeType::DDS: return "image/dds";
    case MimeType::WEBP: return "image/webp";
    default: return {};
    }
}

/// 从 buffer.data variant 取出指向 [bytes, len] 的只读视图（不拷贝）。
/// 仅当 buffer 已实际加载（Array/Vector/ByteView）时返回非空，否则返回 nullptr。
const std::byte* bufferDataView(const DataSource& src, size_t& outLen) {
    if (auto* v = std::get_if<sources::Vector>(&src)) {
        outLen = v->bytes.size();
        return reinterpret_cast<const std::byte*>(v->bytes.data());
    }
    if (auto* a = std::get_if<sources::Array>(&src)) {
        outLen = a->bytes.size();
        return reinterpret_cast<const std::byte*>(a->bytes.data());
    }
    if (auto* b = std::get_if<sources::ByteView>(&src)) {
        outLen = b->bytes.size();
        return b->bytes.data();
    }
    return nullptr;
}

/// 导入纹理，返回 {gltfImageIndex → AssetId}。
/// 覆盖全部 image.data 来源：URI(本地路径)、BufferView(GLB 嵌入)、
/// Vector/Array(LoadExternalImages 已加载字节)、ByteView、data: URI。
/// sRGB 不在这里决定（由 material slot 决定，见 importMaterials）。
std::map<size_t, asset::AssetId> importTextures(const Asset& gltf, ImportBuilder& builder, const std::string& sourceDir,
                                                const std::string& importPath) {
    std::map<size_t, asset::AssetId> texMap;
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const auto& image = gltf.images[i];
        ImportedTextureDesc desc;
        desc.name = image.name.empty() ? "Texture_" + std::to_string(i) : std::string(image.name);

        // 缓存键（用于内嵌源的 GPU 贴图去重）；文件源会被覆盖为绝对路径
        const std::string embeddedKey = "gltf:" + importPath + "#image[" + std::to_string(i) + "]";

        std::visit(visitor{ [&](const sources::URI& uri) {
                               // data: URI 在 LoadExternalImages 模式下应已转 Vector；本地路径直接用绝对路径
                               if (!uri.uri.isLocalPath())
                                   return;
                               auto p = std::filesystem::path(uri.uri.path());
                               desc.sourcePath =
                                       p.is_relative() ? (std::filesystem::path(sourceDir) / p).string() : p.string();
                           },
                            [&](const sources::BufferView& bv) {
                                // GLB 嵌入图像：从 bufferView → buffer 取字节切片
                                if (bv.bufferViewIndex >= gltf.bufferViews.size())
                                    return;
                                const auto& view = gltf.bufferViews[bv.bufferViewIndex];
                                if (view.bufferIndex >= gltf.buffers.size())
                                    return;
                                size_t totalLen = 0;
                                const auto* base = bufferDataView(gltf.buffers[view.bufferIndex].data, totalLen);
                                if (!base)
                                    return;
                                if (view.byteOffset + view.byteLength > totalLen)
                                    return;
                                desc.data.assign(base + view.byteOffset, base + view.byteOffset + view.byteLength);
                                desc.mimeType = mimeTypeString(bv.mimeType);
                                desc.sourcePath = embeddedKey;
                            },
                            [&](const sources::Vector& v) {
                                desc.data.assign(reinterpret_cast<const std::byte*>(v.bytes.data()),
                                                 reinterpret_cast<const std::byte*>(v.bytes.data()) + v.bytes.size());
                                desc.mimeType = mimeTypeString(v.mimeType);
                                desc.sourcePath = embeddedKey;
                            },
                            [&](const sources::Array& a) {
                                desc.data.assign(reinterpret_cast<const std::byte*>(a.bytes.data()),
                                                 reinterpret_cast<const std::byte*>(a.bytes.data()) + a.bytes.size());
                                desc.mimeType = mimeTypeString(a.mimeType);
                                desc.sourcePath = embeddedKey;
                            },
                            [&](const sources::ByteView& b) {
                                desc.data.assign(b.bytes.data(), b.bytes.data() + b.bytes.size());
                                desc.mimeType = mimeTypeString(b.mimeType);
                                desc.sourcePath = embeddedKey;
                            },
                            [](auto&) {} },  // monostate / Fallback / CustomBuffer：跳过
                   image.data);

        // 既无文件路径也无内嵌字节：无法加载，跳过
        if (desc.sourcePath.empty() && desc.data.empty())
            continue;

        asset::AssetId texId = builder.createTexture(desc);
        if (texId)
            texMap[i] = texId;
    }
    return texMap;
}

/// 导入材质，返回 {gltfMaterialIndex → AssetId}
std::map<size_t, asset::AssetId> importMaterials(const Asset& gltf, ImportBuilder& builder,
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
        desc.name = mat.name.empty() ? "Material_" + std::to_string(i) : std::string(mat.name);

        desc.baseColorFactor = { pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                                 pbr.baseColorFactor[3] };
        desc.roughness = static_cast<double>(pbr.roughnessFactor);
        desc.metallic = static_cast<double>(pbr.metallicFactor);

        // sRGB 由 material slot 决定：albedo/emissive → sRGB；normal/mr/ao → linear（数据贴图）
        desc.baseColorTextureSrgb = true;
        desc.normalTextureSrgb = false;
        desc.metallicRoughnessTextureSrgb = false;
        desc.emissiveTextureSrgb = true;
        desc.occlusionTextureSrgb = false;

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

        desc.emissiveFactor = { mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2] };

        desc.alphaMode = static_cast<asset::AlphaMode>(mat.alphaMode);
        desc.doubleSided = mat.doubleSided;

        asset::AssetId matId = builder.createMaterial(desc);
        if (matId)
            matMap[i] = matId;
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

/// 检查 accessor 是否可安全读取：
/// - accessorIndex 有效
/// - 类型/分量类型符合预期
/// - 指向的 bufferView / buffer 存在且 buffer 已实际加载数据（非空 / 非 Fallback / 非 monostate）
bool isAccessorReadable(const Asset& gltf, size_t accessorIndex, AccessorType expectedType,
                        ComponentType expectedCompType = ComponentType::Invalid) {
    if (accessorIndex >= gltf.accessors.size())
        return false;
    const auto& acc = gltf.accessors[accessorIndex];
    if (acc.type != expectedType)
        return false;
    if (expectedCompType != ComponentType::Invalid && acc.componentType != expectedCompType)
        return false;
    if (acc.count == 0)
        return false;

    // bufferView 可缺省（全 0 accessor），此时 iterateAccessor 会用 ElementType{} 填充，是安全的。
    if (!acc.bufferViewIndex.has_value())
        return true;
    if (*acc.bufferViewIndex >= gltf.bufferViews.size())
        return false;

    const auto& view = gltf.bufferViews[*acc.bufferViewIndex];
    if (view.bufferIndex >= gltf.buffers.size())
        return false;

    const auto& buf = gltf.buffers[view.bufferIndex].data;
    // 已加载的数据源：Array / Vector / ByteView；其余（monostate / Fallback 等）说明数据缺失。
    return std::holds_alternative<sources::Array>(buf) || std::holds_alternative<sources::Vector>(buf) ||
           std::holds_alternative<sources::ByteView>(buf);
}

PrimitiveGeomData extractPrimitiveData(const Asset& gltf, const Primitive& prim) {
    PrimitiveGeomData geom;

    auto* posAttr = prim.findAttribute("POSITION");
    if (posAttr && isAccessorReadable(gltf, posAttr->accessorIndex, AccessorType::Vec3, ComponentType::Float)) {
        auto& acc = gltf.accessors[posAttr->accessorIndex];
        geom.positions = readPositions(gltf, acc);
    }

    auto* nrmAttr = prim.findAttribute("NORMAL");
    if (nrmAttr && isAccessorReadable(gltf, nrmAttr->accessorIndex, AccessorType::Vec3, ComponentType::Float)) {
        auto& acc = gltf.accessors[nrmAttr->accessorIndex];
        geom.normals = readPositions(gltf, acc);  // NORMAL 也是 fvec3
    }

    auto* uvAttr = prim.findAttribute("TEXCOORD_0");
    if (uvAttr && isAccessorReadable(gltf, uvAttr->accessorIndex, AccessorType::Vec2, ComponentType::Float)) {
        auto& acc = gltf.accessors[uvAttr->accessorIndex];
        geom.texcoords = readTexcoords(gltf, acc);
    }

    if (prim.indicesAccessor.has_value() && isAccessorReadable(gltf, *prim.indicesAccessor, AccessorType::Scalar)) {
        auto& acc = gltf.accessors[*prim.indicesAccessor];
        geom.indices = readIndices(gltf, acc);
    }

    return geom;
}

scene::LightComponent lightComponentFromGltf(const fastgltf::Light& light) {
    scene::LightComponent component;
    switch (light.type) {
    case fastgltf::LightType::Directional: component.kind = scene::LightKind::Directional; break;
    case fastgltf::LightType::Point: component.kind = scene::LightKind::Point; break;
    case fastgltf::LightType::Spot: component.kind = scene::LightKind::Spot; break;
    }

    component.color = math::Vec3(light.color[0], light.color[1], light.color[2]);
    component.intensity = static_cast<double>(light.intensity);
    component.range = light.range.has_value() ? static_cast<double>(*light.range) : 0.0;
    component.innerConeAngle = light.innerConeAngle.has_value() ? static_cast<double>(*light.innerConeAngle) : 0.0;
    component.outerConeAngle =
            light.outerConeAngle.has_value() ? static_cast<double>(*light.outerConeAngle) : (std::acos(-1.0) / 4.0);
    return component;
}

}  // namespace

// ============================================================
// GltfImporter
// ============================================================

core::Result<ImportResult> GltfImporter::import(const std::string& path, mulan::io::Document& doc,
                                                const ImportOptions& options) {
    ImportResult result{};
    const std::string sourceDir = fileDirectory(path);

    // --- 1. 加载 glTF 文件 ---
    auto fileStream = GltfFileStream(path);
    if (!fileStream.isOpen()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Failed to open glTF file: " + path));
    }

    Parser parser(Extensions::KHR_lights_punctual);
    constexpr auto gltfOpts = Options::LoadExternalBuffers | Options::LoadExternalImages;

    auto assetResult = parser.loadGltf(fileStream, std::filesystem::path(path).parent_path(), gltfOpts);

    if (assetResult.error() != Error::None) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg,
                                  "Failed to parse glTF: " + std::string(getErrorMessage(assetResult.error()))));
    }

    auto& gltf = assetResult.get();
    result.report.lightCount = gltf.lights.size();

    // --- 2. 导入纹理 ---
    ImportBuilder builder(doc);
    auto texMap = importTextures(gltf, builder, sourceDir, path);
    result.report.textureCount = texMap.size();

    // --- 3. 导入材质 ---
    auto matMap = importMaterials(gltf, builder, texMap);
    result.report.materialCount = matMap.size();

    // --- 4. 收集所有 mesh → MeshAsset ---
    std::vector<asset::AssetId> meshAssets(gltf.meshes.size(), asset::AssetId::invalid());

    for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx) {
        const auto& mesh = gltf.meshes[meshIdx];
        std::string meshName = mesh.name.empty() ? "Mesh_" + std::to_string(meshIdx) : std::string(mesh.name);

        std::vector<asset::MeshPrimitive> primitives;

        for (const auto& primitive : mesh.primitives) {
            auto geomData = extractPrimitiveData(gltf, primitive);
            if (geomData.positions.empty())
                continue;

            if (geomData.normals.empty())
                geomData.normals.resize(geomData.positions.size(), math::FVec3(0.0f, 0.0f, 1.0f));

            if (geomData.texcoords.empty())
                geomData.texcoords.resize(geomData.positions.size(), math::FVec2(0.0f, 0.0f));

            StandardMeshSource src;
            src.positions = std::span(geomData.positions);
            src.normals = std::span(geomData.normals);
            src.texcoords = std::span(geomData.texcoords);
            src.indices = std::span(geomData.indices);
            src.topology = graphics::PrimitiveTopology::TriangleList;

            auto engineMesh = buildStandardMesh(src);
            if (engineMesh.empty())
                continue;

            asset::MeshPrimitive prim;
            prim.mesh = std::move(engineMesh);
            prim.material = asset::AssetId::invalid();
            if (primitive.materialIndex.has_value()) {
                auto it = matMap.find(*primitive.materialIndex);
                if (it != matMap.end())
                    prim.material = it->second;
            } else if (!matMap.empty()) {
                prim.material = matMap.begin()->second;
            }
            primitives.push_back(std::move(prim));
        }

        if (!primitives.empty()) {
            auto* assetLib = doc.assets();
            auto* meshAsset = assetLib->create<asset::MeshAsset>(meshName, std::move(primitives));
            if (meshAsset)
                meshAssets[meshIdx] = meshAsset->id();
            result.report.primitiveCount += primitives.size();
        }
    }

    result.report.meshAssetCount = gltf.meshes.size();

    // --- 5. 遍历 node 树，创建实体层级 ---
    auto* scene = doc.scene();
    if (!scene)
        return result;

    // TRS → Mat4（使用项目已有的 math::Transform3）
    auto nodeTransform = [](const fastgltf::Node& node) -> math::Mat4 {
        math::Mat4 mat;
        std::visit(fastgltf::visitor{ [&](const fastgltf::math::fmat4x4& m) {
                                         for (int c = 0; c < 4; ++c)
                                             for (int r = 0; r < 4; ++r)
                                                 mat[c][r] = static_cast<double>(m[c][r]);
                                     },
                                      [&](const fastgltf::TRS& trs) {
                                          math::Transform3 t;
                                          t.translation = { trs.translation[0], trs.translation[1],
                                                            trs.translation[2] };
                                          t.rotation = math::Quat(trs.rotation[3], trs.rotation[0], trs.rotation[1],
                                                                  trs.rotation[2]);
                                          t.scale = { trs.scale[0], trs.scale[1], trs.scale[2] };
                                          mat = t.toMatrix();
                                      } },
                   node.transform);
        return mat;
    };

    const double unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;
    const math::Mat4 rootScale = math::Mat4::scale(math::Vec3(unitScale));

    std::function<void(size_t, scene::EntityId)> importNode;
    importNode = [&](size_t nodeIdx, scene::EntityId parent) {
        const auto& node = gltf.nodes[nodeIdx];

        scene::EntityId entity =
                scene->createEntity(node.name.empty() ? "Node_" + std::to_string(nodeIdx) : std::string(node.name));

        if (parent)
            scene->setParent(entity, parent);

        math::Mat4 localTransform = nodeTransform(node);
        if (!parent)
            localTransform = rootScale * localTransform;
        scene->setLocalTransform(entity, localTransform);

        if (node.lightIndex.has_value() && *node.lightIndex < gltf.lights.size()) {
            scene->setLight(entity, lightComponentFromGltf(gltf.lights[*node.lightIndex]));
        }

        if (node.meshIndex.has_value() && *node.meshIndex < meshAssets.size()) {
            auto meshId = meshAssets[*node.meshIndex];
            if (meshId) {
                scene->setGeometry(entity, meshId);
                const auto* meshAsset = dynamic_cast<const asset::MeshAsset*>(doc.assets()->asset(meshId));
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
            for (auto c : n.children)
                isChild[c] = true;
        for (size_t i = 0; i < gltf.nodes.size(); ++i)
            if (!isChild[i])
                importNode(i, scene::EntityId::invalid());
    }

    return result;
}

std::vector<std::string> GltfImporter::supportedExtensions() const {
    return { ".gltf", ".glb" };
}

std::string GltfImporter::name() const {
    return "glTF 2.0 (fastgltf)";
}

}  // namespace mulan::io
