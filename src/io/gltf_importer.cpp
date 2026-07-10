#include "gltf_importer.h"
#include "import_builder.h"  // buildStandardMesh / StandardMeshSource
#include "parsed_scene.h"

#include <mulan/asset/mesh_asset.h>
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
#include <variant>

namespace mulan::io {
namespace {

using namespace fastgltf;

std::string fileDirectory(const std::string& filePath) {
    return std::filesystem::path(filePath).parent_path().string();
}

std::vector<uint32_t> readIndices(const Asset& asset, const Accessor& accessor) {
    std::vector<uint32_t> result(accessor.count);
    iterateAccessorWithIndex<uint32_t>(asset, accessor, [&](uint32_t idx, size_t i) { result[i] = idx; });
    return result;
}

std::vector<math::FVec3> readPositions(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec3> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, acc, [&](fastgltf::math::fvec3 v, size_t i) { result[i] = { v[0], v[1], v[2] }; });
    return result;
}

std::vector<math::FVec2> readTexcoords(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec2> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, acc, [&](fastgltf::math::fvec2 v, size_t i) { result[i] = { v[0], v[1] }; });
    return result;
}

std::vector<math::FVec4> readTangents(const Asset& asset, const Accessor& acc) {
    std::vector<math::FVec4> result(acc.count);
    iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, acc, [&](fastgltf::math::fvec4 v, size_t i) { result[i] = { v[0], v[1], v[2], v[3] }; });
    return result;
}

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

// ============================================================
// 纹理:产出 ParsedTexture,返回 {gltfImageIndex → ParsedScene.textures 索引}
// ============================================================
std::map<size_t, size_t> importTextures(const Asset& gltf, ParsedScene& scene, const std::string& sourceDir,
                                        const std::string& importPath) {
    std::map<size_t, size_t> texMap;
    for (size_t i = 0; i < gltf.images.size(); ++i) {
        const auto& image = gltf.images[i];
        ParsedTexture desc;
        desc.name = image.name.empty() ? "Texture_" + std::to_string(i) : std::string(image.name);

        const std::string embeddedKey = "gltf:" + importPath + "#image[" + std::to_string(i) + "]";

        std::visit(visitor{ [&](const sources::URI& uri) {
                               if (!uri.uri.isLocalPath())
                                   return;
                               auto p = std::filesystem::path(uri.uri.path());
                               desc.sourcePath =
                                       p.is_relative() ? (std::filesystem::path(sourceDir) / p).string() : p.string();
                           },
                            [&](const sources::BufferView& bv) {
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

std::map<size_t, size_t> importMaterials(const Asset& gltf, ParsedScene& scene, const std::map<size_t, size_t>& texMap,
                                         std::vector<std::string>& warnings) {
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

bool hasUsableNormalTexture(const Asset& gltf, const Primitive& primitive, const std::map<size_t, size_t>& texMap) {
    if (!primitive.materialIndex.has_value() || *primitive.materialIndex >= gltf.materials.size())
        return false;
    const auto& material = gltf.materials[*primitive.materialIndex];
    if (!material.normalTexture.has_value())
        return false;
    const size_t textureIndex = material.normalTexture->textureIndex;
    if (textureIndex >= gltf.textures.size())
        return false;
    const auto& texture = gltf.textures[textureIndex];
    if (!texture.imageIndex.has_value())
        return false;
    return texMap.find(*texture.imageIndex) != texMap.end();
}

struct PrimitiveGeomData {
    std::vector<math::FVec3> positions;
    std::vector<math::FVec3> normals;
    std::vector<math::FVec2> texcoords;
    std::vector<math::FVec4> tangents;
    std::vector<uint32_t> indices;
};

math::FVec3 fallbackTangent(const math::FVec3& normal) {
    const math::FVec3 n = normal.normalizedOr(math::FVec3::unitZ());
    const math::FVec3 axis = std::abs(n.z) < 0.9f ? math::FVec3::unitZ() : math::FVec3::unitY();
    return axis.cross(n).normalizedOr(math::FVec3::unitX());
}

std::vector<math::FVec4> generateTangents(const PrimitiveGeomData& geom) {
    std::vector<math::FVec3> tangentSum(geom.positions.size(), math::FVec3(0.0f));
    std::vector<math::FVec3> bitangentSum(geom.positions.size(), math::FVec3(0.0f));

    const auto indexAt = [&](size_t i) -> uint32_t {
        return geom.indices.empty() ? static_cast<uint32_t>(i) : geom.indices[i];
    };
    const size_t indexCount = geom.indices.empty() ? geom.positions.size() : geom.indices.size();

    for (size_t i = 0; i + 2 < indexCount; i += 3) {
        const uint32_t i0 = indexAt(i + 0);
        const uint32_t i1 = indexAt(i + 1);
        const uint32_t i2 = indexAt(i + 2);
        if (i0 >= geom.positions.size() || i1 >= geom.positions.size() || i2 >= geom.positions.size())
            continue;

        const math::FVec3& p0 = geom.positions[i0];
        const math::FVec3& p1 = geom.positions[i1];
        const math::FVec3& p2 = geom.positions[i2];
        const math::FVec2& uv0 = geom.texcoords[i0];
        const math::FVec2& uv1 = geom.texcoords[i1];
        const math::FVec2& uv2 = geom.texcoords[i2];

        const math::FVec3 e1 = p1 - p0;
        const math::FVec3 e2 = p2 - p0;
        const math::FVec2 duv1 = uv1 - uv0;
        const math::FVec2 duv2 = uv2 - uv0;

        const float det = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::abs(det) < 1e-8f)
            continue;

        const float invDet = 1.0f / det;
        const math::FVec3 tangent = (e1 * duv2.y - e2 * duv1.y) * invDet;
        const math::FVec3 bitangent = (e2 * duv1.x - e1 * duv2.x) * invDet;

        tangentSum[i0] += tangent;
        tangentSum[i1] += tangent;
        tangentSum[i2] += tangent;
        bitangentSum[i0] += bitangent;
        bitangentSum[i1] += bitangent;
        bitangentSum[i2] += bitangent;
    }

    std::vector<math::FVec4> tangents(geom.positions.size(), math::FVec4(1.0f, 0.0f, 0.0f, 1.0f));
    for (size_t i = 0; i < geom.positions.size(); ++i) {
        const math::FVec3 normal =
                i < geom.normals.size() ? geom.normals[i].normalizedOr(math::FVec3::unitZ()) : math::FVec3::unitZ();
        math::FVec3 tangent = tangentSum[i] - normal * normal.dot(tangentSum[i]);
        tangent = tangent.normalizedOr(fallbackTangent(normal));
        const float sign = normal.cross(tangent).dot(bitangentSum[i]) < 0.0f ? -1.0f : 1.0f;
        tangents[i] = math::FVec4(tangent, sign);
    }
    return tangents;
}

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
    if (!acc.bufferViewIndex.has_value())
        return true;
    if (*acc.bufferViewIndex >= gltf.bufferViews.size())
        return false;
    const auto& view = gltf.bufferViews[*acc.bufferViewIndex];
    if (view.bufferIndex >= gltf.buffers.size())
        return false;
    const auto& buf = gltf.buffers[view.bufferIndex].data;
    return std::holds_alternative<sources::Array>(buf) || std::holds_alternative<sources::Vector>(buf) ||
           std::holds_alternative<sources::ByteView>(buf);
}

PrimitiveGeomData extractPrimitiveData(const Asset& gltf, const Primitive& prim) {
    PrimitiveGeomData geom;

    auto* posAttr = prim.findAttribute("POSITION");
    if (posAttr && isAccessorReadable(gltf, posAttr->accessorIndex, AccessorType::Vec3, ComponentType::Float))
        geom.positions = readPositions(gltf, gltf.accessors[posAttr->accessorIndex]);

    auto* nrmAttr = prim.findAttribute("NORMAL");
    if (nrmAttr && isAccessorReadable(gltf, nrmAttr->accessorIndex, AccessorType::Vec3, ComponentType::Float))
        geom.normals = readPositions(gltf, gltf.accessors[nrmAttr->accessorIndex]);

    auto* uvAttr = prim.findAttribute("TEXCOORD_0");
    if (uvAttr && isAccessorReadable(gltf, uvAttr->accessorIndex, AccessorType::Vec2, ComponentType::Float))
        geom.texcoords = readTexcoords(gltf, gltf.accessors[uvAttr->accessorIndex]);

    auto* tangentAttr = prim.findAttribute("TANGENT");
    if (tangentAttr && isAccessorReadable(gltf, tangentAttr->accessorIndex, AccessorType::Vec4, ComponentType::Float))
        geom.tangents = readTangents(gltf, gltf.accessors[tangentAttr->accessorIndex]);

    if (prim.indicesAccessor.has_value() && isAccessorReadable(gltf, *prim.indicesAccessor, AccessorType::Scalar))
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

core::Result<ParsedScene> GltfImporter::parse(const std::string& path, const ImportOptions& options) {
    ParsedScene scene;
    scene.unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;

    const std::string sourceDir = fileDirectory(path);

    auto fileStream = GltfFileStream(path);
    if (!fileStream.isOpen())
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Failed to open glTF file: " + path));

    Parser parser(Extensions::KHR_lights_punctual);
    constexpr auto gltfOpts = Options::LoadExternalBuffers | Options::LoadExternalImages;
    auto assetResult = parser.loadGltf(fileStream, std::filesystem::path(path).parent_path(), gltfOpts);
    if (assetResult.error() != Error::None) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg,
                                  "Failed to parse glTF: " + std::string(getErrorMessage(assetResult.error()))));
    }

    auto& gltf = assetResult.get();

    // 纹理 → 材质
    auto texMap = importTextures(gltf, scene, sourceDir, path);
    std::vector<std::string> warnings;
    auto matMap = importMaterials(gltf, scene, texMap, warnings);
    (void) matMap;

    // 网格:每个 glTF mesh → ParsedMesh,primitive 材质用 matMap 索引
    std::vector<size_t> meshIndices(gltf.meshes.size(), SIZE_MAX);
    for (size_t meshIdx = 0; meshIdx < gltf.meshes.size(); ++meshIdx) {
        const auto& mesh = gltf.meshes[meshIdx];
        std::string meshName = mesh.name.empty() ? "Mesh_" + std::to_string(meshIdx) : std::string(mesh.name);

        ParsedMesh parsed;
        parsed.name = meshName;

        for (const auto& primitive : mesh.primitives) {
            auto geomData = extractPrimitiveData(gltf, primitive);
            if (geomData.positions.empty())
                continue;
            if (geomData.normals.empty())
                geomData.normals.resize(geomData.positions.size(), math::FVec3(0.0f, 0.0f, 1.0f));
            if (geomData.texcoords.empty())
                geomData.texcoords.resize(geomData.positions.size(), math::FVec2(0.0f, 0.0f));

            if (hasUsableNormalTexture(gltf, primitive, texMap)) {
                if (geomData.tangents.size() != geomData.positions.size())
                    geomData.tangents = generateTangents(geomData);
            } else {
                geomData.tangents.clear();
            }

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

    // 节点树 → ParsedNode(不碰 Document)
    scene.nodes.resize(gltf.nodes.size());
    auto buildNode = [&](size_t nodeIdx, size_t parentIdx, auto& self) -> void {
        const auto& node = gltf.nodes[nodeIdx];
        auto& pn = scene.nodes[nodeIdx];
        pn.name = node.name.empty() ? "Node_" + std::to_string(nodeIdx) : std::string(node.name);
        pn.parent = parentIdx;
        pn.localTransform = nodeTransform(node);

        if (node.lightIndex.has_value() && *node.lightIndex < gltf.lights.size()) {
            pn.lightIndex = scene.lights.size();
            scene.lights.push_back(lightFromGltf(gltf.lights[*node.lightIndex]));
        }
        if (node.meshIndex.has_value() && *node.meshIndex < meshIndices.size())
            pn.meshIndex = meshIndices[*node.meshIndex];

        for (auto childIdx : node.children) {
            if (childIdx < gltf.nodes.size())
                self(static_cast<size_t>(childIdx), nodeIdx, self);
        }
    };

    if (gltf.defaultScene.has_value()) {
        const auto& sceneDef = gltf.scenes[*gltf.defaultScene];
        for (auto rootIdx : sceneDef.nodeIndices) {
            buildNode(static_cast<size_t>(rootIdx), SIZE_MAX, buildNode);
            scene.rootNodes.push_back(static_cast<size_t>(rootIdx));
        }
    } else {
        std::vector<bool> isChild(gltf.nodes.size(), false);
        for (auto& n : gltf.nodes)
            for (auto c : n.children)
                if (c < isChild.size())
                    isChild[c] = true;
        for (size_t i = 0; i < gltf.nodes.size(); ++i) {
            if (!isChild[i]) {
                buildNode(i, SIZE_MAX, buildNode);
                scene.rootNodes.push_back(i);
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
