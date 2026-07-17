#include "gltf_importer.h"
#include "detail/mesh_attribute_generator.h"
#include "import_builder.h"  // buildStandardMesh / StandardMeshSource
#include "import_path_utils.h"
#include "parsed_scene.h"

#include <mulan/asset/mesh_asset.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/graphics/mesh.h>
#include <mulan/graphics/vertex/vertex_buffer.h>
#include <mulan/graphics/vertex/vertex_layout.h>
#include <mulan/math/linalg/transform.h>
#include <mulan/scene/components/light_component.h>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <variant>

namespace mulan::io {
namespace {

std::string fileDirectory(const std::string& filePath) {
    return std::filesystem::path(filePath).parent_path().string();
}

std::optional<std::string> validateGltfResourceLimits(const fastgltf::Asset& gltf,
                                                      const std::filesystem::path& sourceDirectory,
                                                      const ImportOptions& options) {
    if (gltf.nodes.size() > options.maxNodeCount)
        return "glTF node count exceeds configured limit";

    uint64_t totalAccessorBytes = 0;
    for (const auto& accessor : gltf.accessors) {
        if (accessor.count > options.maxAccessorElements)
            return "glTF accessor element count exceeds configured limit";
        const uint64_t elementBytes = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
        if (elementBytes != 0 && accessor.count > (options.maxTotalAccessorBytes - totalAccessorBytes) / elementBytes)
            return "glTF accessor data exceeds configured limit";
        totalAccessorBytes += static_cast<uint64_t>(accessor.count) * elementBytes;
    }

    const auto validateSource = [&](const fastgltf::DataSource& source) -> std::optional<std::string> {
        const auto* uri = std::get_if<fastgltf::sources::URI>(&source);
        if (!uri)
            return std::nullopt;
        if (!uri->uri.isLocalPath())
            return "glTF external resource URI is not a local relative path";
        const auto resolved = detail::resolveContainedImportPath(sourceDirectory, uri->uri.path());
        if (!resolved)
            return "glTF external resource escapes the model directory";
        if (!detail::importFileWithinLimit(*resolved, options.maxExternalFileBytes))
            return "glTF external resource exceeds configured file-size limit";
        return std::nullopt;
    };
    for (const auto& buffer : gltf.buffers) {
        if (auto error = validateSource(buffer.data))
            return error;
    }
    for (const auto& image : gltf.images) {
        if (auto error = validateSource(image.data))
            return error;
    }
    return std::nullopt;
}

bool requiresExternalResourceLoading(const fastgltf::Asset& gltf) {
    const auto isUri = [](const auto& resource) {
        return std::holds_alternative<fastgltf::sources::URI>(resource.data);
    };
    return std::any_of(gltf.buffers.begin(), gltf.buffers.end(), isUri) ||
           std::any_of(gltf.images.begin(), gltf.images.end(), isUri);
}

std::vector<uint32_t> readIndices(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor) {
    std::vector<uint32_t> result(accessor.count);
    iterateAccessorWithIndex<uint32_t>(asset, accessor, [&](uint32_t idx, size_t i) { result[i] = idx; });
    return result;
}

std::vector<math::FVec3> readPositions(const fastgltf::Asset& asset, const fastgltf::Accessor& acc) {
    std::vector<math::FVec3> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, acc, [&](fastgltf::math::fvec3 v, size_t i) { result[i] = { v[0], v[1], v[2] }; });
    return result;
}

std::vector<math::FVec2> readTexcoords(const fastgltf::Asset& asset, const fastgltf::Accessor& acc) {
    std::vector<math::FVec2> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, acc, [&](fastgltf::math::fvec2 v, size_t i) { result[i] = { v[0], v[1] }; });
    return result;
}

std::vector<math::FVec4> readTangents(const fastgltf::Asset& asset, const fastgltf::Accessor& acc) {
    std::vector<math::FVec4> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, acc, [&](fastgltf::math::fvec4 v, size_t i) { result[i] = { v[0], v[1], v[2], v[3] }; });
    return result;
}

std::string mimeTypeString(fastgltf::MimeType mt) {
    switch (mt) {
    case fastgltf::MimeType::JPEG: return "image/jpeg";
    case fastgltf::MimeType::PNG: return "image/png";
    case fastgltf::MimeType::KTX2: return "image/ktx2";
    case fastgltf::MimeType::DDS: return "image/dds";
    case fastgltf::MimeType::WEBP: return "image/webp";
    default: return {};
    }
}

const std::byte* bufferDataView(const fastgltf::DataSource& src, size_t& outLen) {
    if (auto* v = std::get_if<fastgltf::sources::Vector>(&src)) {
        outLen = v->bytes.size();
        return reinterpret_cast<const std::byte*>(v->bytes.data());
    }
    if (auto* a = std::get_if<fastgltf::sources::Array>(&src)) {
        outLen = a->bytes.size();
        return reinterpret_cast<const std::byte*>(a->bytes.data());
    }
    if (auto* b = std::get_if<fastgltf::sources::ByteView>(&src)) {
        outLen = b->bytes.size();
        return b->bytes.data();
    }
    return nullptr;
}

// ============================================================
// 纹理:产出 ParsedTexture,返回 {gltfImageIndex → ParsedScene.textures 索引}
// ============================================================
std::map<size_t, size_t> importTextures(const fastgltf::Asset& gltf, ParsedScene& scene, const std::string& sourceDir,
                                        const std::string& importPath) {
    MULAN_PROFILE_ZONE();

    std::map<size_t, size_t> texMap;
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const auto& image = gltf.images[i];
        ParsedTexture desc;
        desc.name = image.name.empty() ? "Texture_" + std::to_string(i) : std::string(image.name);

        const std::string embeddedKey = "gltf:" + importPath + "#image[" + std::to_string(i) + "]";

        std::visit(fastgltf::visitor{
                           [&](const fastgltf::sources::URI& uri) {
                               if (!uri.uri.isLocalPath())
                                   return;
                               const auto resolved = detail::resolveContainedImportPath(sourceDir, uri.uri.path());
                               if (resolved)
                                   desc.sourcePath = resolved->string();
                           },
                           [&](const fastgltf::sources::BufferView& bv) {
                               if (bv.bufferViewIndex >= gltf.bufferViews.size())
                                   return;
                               const auto& view = gltf.bufferViews[bv.bufferViewIndex];
                               if (view.bufferIndex >= gltf.buffers.size())
                                   return;
                               size_t totalLen = 0;
                               const auto* base = bufferDataView(gltf.buffers[view.bufferIndex].data, totalLen);
                               if (!base || view.byteOffset + view.byteLength > totalLen)
                                   return;
                               desc.data.assign(base + view.byteOffset, base + view.byteOffset + view.byteLength);
                               desc.mimeType = mimeTypeString(bv.mimeType);
                               desc.sourcePath = embeddedKey;
                           },
                           [&](const fastgltf::sources::Vector& v) {
                               desc.data.assign(reinterpret_cast<const std::byte*>(v.bytes.data()),
                                                reinterpret_cast<const std::byte*>(v.bytes.data()) + v.bytes.size());
                               desc.mimeType = mimeTypeString(v.mimeType);
                               desc.sourcePath = embeddedKey;
                           },
                           [&](const fastgltf::sources::Array& a) {
                               desc.data.assign(reinterpret_cast<const std::byte*>(a.bytes.data()),
                                                reinterpret_cast<const std::byte*>(a.bytes.data()) + a.bytes.size());
                               desc.mimeType = mimeTypeString(a.mimeType);
                               desc.sourcePath = embeddedKey;
                           },
                           [&](const fastgltf::sources::ByteView& b) {
                               desc.data.assign(b.bytes.data(), b.bytes.data() + b.bytes.size());
                               desc.mimeType = mimeTypeString(b.mimeType);
                               desc.sourcePath = embeddedKey;
                           },
                           [](auto&) {} },
                   image.data);

        if (desc.sourcePath.empty() && desc.data.empty())
            continue;

        size_t idx = scene.textures.size();
        scene.textures.push_back(std::move(desc));
        texMap[i] = idx;
    }
    return texMap;
}

// ============================================================
// 材质:产出 ParsedMaterial,纹理引用用 ParsedScene.textures 索引
// ============================================================
struct TextureSlotResolve {
    size_t parsedTextureIndex = SIZE_MAX;  // 指向 ParsedScene.textures
    bool found = false;
};

std::map<size_t, size_t> importMaterials(const fastgltf::Asset& gltf, ParsedScene& scene,
                                         const std::map<size_t, size_t>& texMap, std::vector<std::string>& warnings) {
    std::map<size_t, size_t> matMap;

    auto resolveTexture = [&](size_t texIdx) -> TextureSlotResolve {
        TextureSlotResolve r;
        if (texIdx >= gltf.textures.size())
            return r;
        const auto& texture = gltf.textures[texIdx];
        if (!texture.imageIndex.has_value())
            return r;
        auto it = texMap.find(*texture.imageIndex);
        if (it != texMap.end()) {
            r.parsedTextureIndex = it->second;
            r.found = true;
        }
        return r;
    };

    for (size_t i = 0; i < gltf.materials.size(); ++i) {
        const auto& mat = gltf.materials[i];
        const auto& pbr = mat.pbrData;

        ParsedMaterial desc;
        desc.name = mat.name.empty() ? "Material_" + std::to_string(i) : std::string(mat.name);

        desc.baseColorFactor = { pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                                 pbr.baseColorFactor[3] };
        desc.roughness = static_cast<double>(pbr.roughnessFactor);
        desc.metallic = static_cast<double>(pbr.metallicFactor);

        if (pbr.baseColorTexture.has_value())
            desc.baseColorTexture = resolveTexture(pbr.baseColorTexture->textureIndex).parsedTextureIndex;
        if (pbr.metallicRoughnessTexture.has_value())
            desc.metallicRoughnessTexture =
                    resolveTexture(pbr.metallicRoughnessTexture->textureIndex).parsedTextureIndex;
        if (mat.normalTexture.has_value())
            desc.normalTexture = resolveTexture(mat.normalTexture->textureIndex).parsedTextureIndex;
        if (mat.emissiveTexture.has_value())
            desc.emissiveTexture = resolveTexture(mat.emissiveTexture->textureIndex).parsedTextureIndex;
        if (mat.occlusionTexture.has_value())
            desc.occlusionTexture = resolveTexture(mat.occlusionTexture->textureIndex).parsedTextureIndex;

        desc.emissiveFactor = { mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2] };
        desc.emissiveStrength = static_cast<double>(mat.emissiveStrength);
        desc.alphaMode = static_cast<graphics::AlphaMode>(mat.alphaMode);
        desc.doubleSided = mat.doubleSided;

        size_t idx = scene.materials.size();
        scene.materials.push_back(std::move(desc));
        matMap[i] = idx;
    }

    if (matMap.empty()) {
        ParsedMaterial def;
        def.name = "DefaultPBR";
        size_t idx = scene.materials.size();
        scene.materials.push_back(std::move(def));
        matMap[0] = idx;
    }
    return matMap;
}

bool hasUsableNormalTexture(const fastgltf::Asset& gltf, const fastgltf::Primitive& primitive,
                            const std::map<size_t, size_t>& texMap) {
    if (!primitive.materialIndex.has_value() || *primitive.materialIndex >= gltf.materials.size())
        return false;
    const auto& material = gltf.materials[*primitive.materialIndex];
    if (!material.normalTexture.has_value())
        return false;
    // 当前顶点格式只导入 TEXCOORD_0；不能把其他 UV 集合误当作 0 号集合生成切线。
    if (material.normalTexture->texCoordIndex != 0u)
        return false;
    const size_t textureIndex = material.normalTexture->textureIndex;
    if (textureIndex >= gltf.textures.size())
        return false;
    const auto& texture = gltf.textures[textureIndex];
    if (!texture.imageIndex.has_value())
        return false;
    return texMap.find(*texture.imageIndex) != texMap.end();
}

bool isAccessorReadable(const fastgltf::Asset& gltf, size_t accessorIndex, fastgltf::AccessorType expectedType,
                        fastgltf::ComponentType expectedCompType = fastgltf::ComponentType::Invalid) {
    if (accessorIndex >= gltf.accessors.size())
        return false;
    const auto& acc = gltf.accessors[accessorIndex];
    if (acc.type != expectedType)
        return false;
    if (expectedCompType != fastgltf::ComponentType::Invalid && acc.componentType != expectedCompType)
        return false;
    if (acc.count == 0)
        return false;
    if (!acc.bufferViewIndex.has_value())
        return true;
    if (*acc.bufferViewIndex >= gltf.bufferViews.size())
        return false;
    const auto& view = gltf.bufferViews[*acc.bufferViewIndex];
    if (view.bufferIndex >= gltf.buffers.size())
        return false;
    const auto& buf = gltf.buffers[view.bufferIndex].data;
    return std::holds_alternative<fastgltf::sources::Array>(buf) ||
           std::holds_alternative<fastgltf::sources::Vector>(buf) ||
           std::holds_alternative<fastgltf::sources::ByteView>(buf);
}

bool isTexcoordAccessorReadable(const fastgltf::Asset& gltf, size_t accessorIndex) {
    if (!isAccessorReadable(gltf, accessorIndex, fastgltf::AccessorType::Vec2))
        return false;
    const auto& accessor = gltf.accessors[accessorIndex];
    if (accessor.componentType == fastgltf::ComponentType::Float)
        return true;
    const bool isNormalizedInteger = accessor.componentType == fastgltf::ComponentType::UnsignedByte ||
                                     accessor.componentType == fastgltf::ComponentType::UnsignedShort;
    return isNormalizedInteger && accessor.normalized;
}

detail::TriangleMeshData extractPrimitiveData(const fastgltf::Asset& gltf, const fastgltf::Primitive& prim) {
    detail::TriangleMeshData geom;

    auto* posAttr = prim.findAttribute("POSITION");
    if (posAttr &&
        isAccessorReadable(gltf, posAttr->accessorIndex, fastgltf::AccessorType::Vec3, fastgltf::ComponentType::Float))
        geom.positions = readPositions(gltf, gltf.accessors[posAttr->accessorIndex]);

    auto* nrmAttr = prim.findAttribute("NORMAL");
    if (nrmAttr &&
        isAccessorReadable(gltf, nrmAttr->accessorIndex, fastgltf::AccessorType::Vec3, fastgltf::ComponentType::Float))
        geom.normals = readPositions(gltf, gltf.accessors[nrmAttr->accessorIndex]);

    auto* uvAttr = prim.findAttribute("TEXCOORD_0");
    if (uvAttr && isTexcoordAccessorReadable(gltf, uvAttr->accessorIndex))
        geom.texcoords = readTexcoords(gltf, gltf.accessors[uvAttr->accessorIndex]);

    auto* tangentAttr = prim.findAttribute("TANGENT");
    if (tangentAttr && isAccessorReadable(gltf, tangentAttr->accessorIndex, fastgltf::AccessorType::Vec4,
                                          fastgltf::ComponentType::Float))
        geom.tangents = readTangents(gltf, gltf.accessors[tangentAttr->accessorIndex]);

    if (prim.indicesAccessor.has_value() &&
        isAccessorReadable(gltf, *prim.indicesAccessor, fastgltf::AccessorType::Scalar))
        geom.indices = readIndices(gltf, gltf.accessors[*prim.indicesAccessor]);

    return geom;
}

ParsedLight lightFromGltf(const fastgltf::Light& light) {
    ParsedLight pl;
    switch (light.type) {
    case fastgltf::LightType::Directional: pl.kind = scene::LightKind::Directional; break;
    case fastgltf::LightType::Point: pl.kind = scene::LightKind::Point; break;
    case fastgltf::LightType::Spot: pl.kind = scene::LightKind::Spot; break;
    }
    pl.color = math::Vec3(light.color[0], light.color[1], light.color[2]);
    pl.intensity = static_cast<double>(light.intensity);
    pl.range = light.range.has_value() ? static_cast<double>(*light.range) : 0.0;
    pl.innerConeAngle = light.innerConeAngle.has_value() ? static_cast<double>(*light.innerConeAngle) : 0.0;
    pl.outerConeAngle =
            light.outerConeAngle.has_value() ? static_cast<double>(*light.outerConeAngle) : (std::acos(-1.0) / 4.0);
    return pl;
}

math::Mat4 nodeTransform(const fastgltf::Node& node) {
    math::Mat4 mat;
    std::visit(fastgltf::visitor{ [&](const fastgltf::math::fmat4x4& m) {
                                     for (int c = 0; c < 4; ++c)
                                         for (int r = 0; r < 4; ++r)
                                             mat[c][r] = static_cast<double>(m[c][r]);
                                 },
                                  [&](const fastgltf::TRS& trs) {
                                      math::Transform3 t;
                                      t.translation = { trs.translation[0], trs.translation[1], trs.translation[2] };
                                      t.rotation = math::Quat(trs.rotation[3], trs.rotation[0], trs.rotation[1],
                                                              trs.rotation[2]);
                                      t.scale = { trs.scale[0], trs.scale[1], trs.scale[2] };
                                      mat = t.toMatrix();
                                  } },
               node.transform);
    return mat;
}

}  // namespace

// ============================================================
// GltfImporter
// ============================================================

Result<ParsedScene> GltfImporter::parse(const std::string& path, const ImportOptions& options) {
    MULAN_PROFILE_ZONE();

    ParsedScene scene;
    scene.unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;

    if (!detail::importFileWithinLimit(path, options.maxExternalFileBytes))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "glTF source file exceeds configured size limit"));
    const std::string sourceDir = fileDirectory(path);

    const std::filesystem::path sourceDirectory = std::filesystem::path(path).parent_path();
    auto fileData = [&] {
        MULAN_PROFILE_ZONE_N("fastgltf::GltfDataBuffer::FromPath");
        return fastgltf::GltfDataBuffer::FromPath(path);
    }();
    if (fileData.error() != fastgltf::Error::None)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Failed to read glTF file: " + path));

    const auto extensions =
            fastgltf::Extensions::KHR_lights_punctual | fastgltf::Extensions::KHR_materials_emissive_strength;
    fastgltf::Parser preflightParser(extensions);
    auto preflight = [&] {
        MULAN_PROFILE_ZONE_N("fastgltf::Parser::loadGltf");
        return preflightParser.loadGltf(fileData.get(), sourceDirectory, fastgltf::Options::None);
    }();
    if (preflight.error() != fastgltf::Error::None) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg,
                                           "Failed to parse glTF: " + std::string(getErrorMessage(preflight.error()))));
    }
    if (auto limitError = validateGltfResourceLimits(preflight.get(), sourceDirectory, options))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, *limitError));

    // 自包含 GLB 的内嵌 Buffer 在 fastgltf 中默认已驻留，直接消费预检查结果即可。
    // 仅URI资源需要第二阶段加载；复用同一内存快照，避免重新读取或主文件阶段间变化。
    const fastgltf::Asset* gltf = &preflight.get();
    std::optional<fastgltf::Parser> resourceParser;
    std::optional<fastgltf::Asset> resourceAsset;
    if (requiresExternalResourceLoading(*gltf)) {
        resourceParser.emplace(extensions);
        constexpr auto gltfOpts = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
        auto loaded = [&] {
            MULAN_PROFILE_ZONE_N("fastgltf::Parser::loadGltf");
            return resourceParser->loadGltf(fileData.get(), sourceDirectory, gltfOpts);
        }();
        if (loaded.error() != fastgltf::Error::None) {
            return std::unexpected(
                    Error::make(ErrorCode::InvalidArg,
                                "Failed to load glTF resources: " + std::string(getErrorMessage(loaded.error()))));
        }
        if (auto limitError = validateGltfResourceLimits(loaded.get(), sourceDirectory, options))
            return std::unexpected(Error::make(ErrorCode::InvalidArg, *limitError));
        resourceAsset.emplace(std::move(loaded.get()));
        gltf = &*resourceAsset;
    }

    // 纹理 → 材质
    auto texMap = importTextures(*gltf, scene, sourceDir, path);
    std::vector<std::string> warnings;
    auto matMap = importMaterials(*gltf, scene, texMap, warnings);
    (void) matMap;

    // 网格:每个 glTF mesh → ParsedMesh,primitive 材质用 matMap 索引
    std::vector<size_t> meshIndices(gltf->meshes.size(), SIZE_MAX);
    {
        MULAN_PROFILE_ZONE_N("GltfImporter::parse/meshes");
        for (size_t meshIdx = 0; meshIdx < gltf->meshes.size(); ++meshIdx) {
            const auto& mesh = gltf->meshes[meshIdx];
            std::string meshName = mesh.name.empty() ? "Mesh_" + std::to_string(meshIdx) : std::string(mesh.name);

            ParsedMesh parsed;
            parsed.name = meshName;

            for (const auto& primitive : mesh.primitives) {
                // 当前 graphics::Mesh 只为导入路径定义了三角列表语义；其他 glTF 图元模式不能按三角形误读。
                if (primitive.type != fastgltf::PrimitiveType::Triangles)
                    continue;

                auto geomData = extractPrimitiveData(*gltf, primitive);
                if (geomData.positions.empty())
                    continue;

                // 属性 accessor 数量不一致时将其视为缺失，避免把错位属性写入顶点流。
                if (geomData.normals.size() != geomData.positions.size())
                    geomData.normals.clear();
                if (geomData.texcoords.size() != geomData.positions.size())
                    geomData.texcoords.clear();
                if (geomData.tangents.size() != geomData.positions.size())
                    geomData.tangents.clear();

                if (auto valid = detail::validateTriangleMesh(geomData); !valid)
                    continue;

                // glTF 要求缺少法线时生成平面法线，且忽略文件中已有的切线。
                if (geomData.normals.empty()) {
                    geomData.tangents.clear();
                    if (auto generated = detail::generateFlatNormals(geomData); !generated)
                        continue;
                }

                if (hasUsableNormalTexture(*gltf, primitive, texMap) && !geomData.texcoords.empty()) {
                    if (geomData.tangents.empty()) {
                        if (auto generated = detail::generateMikkTangents(geomData); !generated)
                            geomData.tangents.clear();
                    }
                } else {
                    geomData.tangents.clear();
                }

                // surface/pbr 顶点布局始终包含 UV；缺少 UV 时使用稳定默认值，但不会启用法线贴图管线。
                if (geomData.texcoords.empty())
                    geomData.texcoords.resize(geomData.positions.size(), math::FVec2(0.0f));

                StandardMeshSource src;
                src.positions = std::span(geomData.positions);
                src.normals = std::span(geomData.normals);
                src.texcoords = std::span(geomData.texcoords);
                src.tangents = std::span(geomData.tangents);
                src.indices = std::span(geomData.indices);
                src.topology = graphics::PrimitiveTopology::TriangleList;

                auto engineMesh = buildStandardMesh(src);
                if (engineMesh.empty())
                    continue;

                asset::MeshPrimitive prim;
                prim.mesh = std::move(engineMesh);
                size_t matIdx = SIZE_MAX;
                if (primitive.materialIndex.has_value()) {
                    auto it = matMap.find(*primitive.materialIndex);
                    if (it != matMap.end())
                        matIdx = it->second;
                } else if (!matMap.empty()) {
                    matIdx = matMap.begin()->second;
                }
                parsed.primitives.push_back(std::move(prim));
                parsed.materialIndices.push_back(matIdx);
            }

            if (!parsed.primitives.empty()) {
                meshIndices[meshIdx] = scene.meshes.size();
                scene.meshes.push_back(std::move(parsed));
            }
        }
    }

    // 节点树 → ParsedNode(不碰 Document)
    scene.nodes.resize(gltf->nodes.size());
    auto buildNode = [&](size_t nodeIdx, size_t parentIdx, auto& self) -> void {
        const auto& node = gltf->nodes[nodeIdx];
        auto& pn = scene.nodes[nodeIdx];
        pn.name = node.name.empty() ? "Node_" + std::to_string(nodeIdx) : std::string(node.name);
        pn.parent = parentIdx;
        pn.localTransform = nodeTransform(node);

        if (node.lightIndex.has_value() && *node.lightIndex < gltf->lights.size()) {
            pn.lightIndex = scene.lights.size();
            scene.lights.push_back(lightFromGltf(gltf->lights[*node.lightIndex]));
        }
        if (node.meshIndex.has_value() && *node.meshIndex < meshIndices.size())
            pn.meshIndex = meshIndices[*node.meshIndex];

        for (auto childIdx : node.children) {
            if (childIdx < gltf->nodes.size())
                self(static_cast<size_t>(childIdx), nodeIdx, self);
        }
    };

    {
        MULAN_PROFILE_ZONE_N("GltfImporter::parse/nodes");
        if (gltf->defaultScene.has_value()) {
            const auto& sceneDef = gltf->scenes[*gltf->defaultScene];
            for (auto rootIdx : sceneDef.nodeIndices) {
                buildNode(static_cast<size_t>(rootIdx), SIZE_MAX, buildNode);
                scene.rootNodes.push_back(static_cast<size_t>(rootIdx));
            }
        } else {
            std::vector<bool> isChild(gltf->nodes.size(), false);
            for (auto& n : gltf->nodes)
                for (auto c : n.children)
                    if (c < isChild.size())
                        isChild[c] = true;
            for (size_t i = 0; i < gltf->nodes.size(); ++i) {
                if (!isChild[i]) {
                    buildNode(i, SIZE_MAX, buildNode);
                    scene.rootNodes.push_back(i);
                }
            }
        }
    }

    return scene;
}

std::vector<std::string> GltfImporter::supportedExtensions() const {
    return { ".gltf", ".glb" };
}

std::string GltfImporter::name() const {
    return "glTF Importer";
}

}  // namespace mulan::io
