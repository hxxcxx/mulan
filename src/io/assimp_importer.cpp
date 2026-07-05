#include "assimp_importer.h"
#include "importer_factory.h"
#include "import_builder.h"

#include <mulan/core/result/error.h>
#include <mulan/io/document.h>
#include <mulan/scene/scene.h>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mulan::io {
namespace {

constexpr std::array<const char*, 14> kSupportedExtensions = { "obj", "fbx",   "dae", "3ds", "ply", "stl", "gltf",
                                                               "glb", "blend", "x",   "ase", "lwo", "off", "dxf" };

math::FVec3 toFVec3(const aiVector3D& value) {
    return { value.x, value.y, value.z };
}

math::FVec2 toTexcoord(const aiVector3D& value) {
    return { value.x, value.y };
}

math::Vec4 toColor(const aiColor4D& value) {
    return { value.r, value.g, value.b, value.a };
}

math::Mat4 toMat4(const aiMatrix4x4& value) {
    math::Mat4 result{ 1.0 };
    result[0][0] = value.a1;
    result[1][0] = value.a2;
    result[2][0] = value.a3;
    result[3][0] = value.a4;
    result[0][1] = value.b1;
    result[1][1] = value.b2;
    result[2][1] = value.b3;
    result[3][1] = value.b4;
    result[0][2] = value.c1;
    result[1][2] = value.c2;
    result[2][2] = value.c3;
    result[3][2] = value.c4;
    result[0][3] = value.d1;
    result[1][3] = value.d2;
    result[2][3] = value.d3;
    result[3][3] = value.d4;
    return result;
}

math::AABB3 transformBounds(const math::AABB3& bounds, const math::Mat4& transform) {
    if (bounds.isEmpty())
        return math::AABB3::empty();

    math::AABB3 result = math::AABB3::empty();
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                const math::Point3 corner{ x == 0 ? bounds.min.x : bounds.max.x, y == 0 ? bounds.min.y : bounds.max.y,
                                           z == 0 ? bounds.min.z : bounds.max.z };
                result.expand(corner.transformedBy(transform));
            }
        }
    }
    return result;
}

std::string materialName(const aiMaterial& material, size_t index) {
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {
        return name.C_Str();
    }
    return "Material_" + std::to_string(index);
}

asset::AlphaMode alphaModeFromAssimp(const aiMaterial& material) {
#ifdef AI_MATKEY_GLTF_ALPHAMODE
    aiString blendMode;
    if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, blendMode) == AI_SUCCESS) {
        const std::string value = blendMode.C_Str();
        if (value == "BLEND")
            return asset::AlphaMode::Blend;
        if (value == "MASK")
            return asset::AlphaMode::Mask;
    }
#else
    (void) material;
#endif
    return asset::AlphaMode::Opaque;
}

asset::AssetId importTexture(aiMaterial& material, aiTextureType type, ImportBuilder& builder,
                             const std::filesystem::path& baseDirectory) {
    if (material.GetTextureCount(type) == 0)
        return asset::AssetId::invalid();

    aiString path;
    if (material.GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0) {
        return asset::AssetId::invalid();
    }

    ImportedTextureDesc desc;
    std::filesystem::path texturePath(path.C_Str());
    const std::string texturePathString = texturePath.string();
    if (!texturePathString.empty() && texturePathString.front() == '*') {
        // TODO(assimp-embedded): 当前未读取 aiScene::mTextures 中的内嵌像素字节。
        //      渲染端会因找不到文件加载失败；后续应从 scene.mTextures[idx].pcData 取出
        //      (mWidth*mHeight*bpp 或 mHeight=compressed size) 字节填入 desc.data。
        desc.name = "EmbeddedTexture_" + texturePathString.substr(1);
        desc.sourcePath = "assimp:embedded#" + texturePathString;  // 缓存键占位，避免空路径误判
    } else {
        desc.name = texturePath.filename().string();
        desc.sourcePath = texturePath.is_relative() ? (baseDirectory / texturePath).string() : texturePath.string();
    }
    return builder.createTexture(desc);
}

std::vector<asset::AssetId> importMaterials(const aiScene& scene, ImportBuilder& builder,
                                            const std::filesystem::path& baseDirectory, const ImportOptions& options) {
    std::vector<asset::AssetId> materials(scene.mNumMaterials, asset::AssetId::invalid());
    if (!options.importMaterials)
        return materials;

    for (size_t i = 0; i < scene.mNumMaterials; ++i) {
        aiMaterial* source = scene.mMaterials[i];
        if (!source)
            continue;

        ImportedMaterialDesc desc;
        desc.name = materialName(*source, i);

        aiColor4D baseColor;
        if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS ||
            aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS) {
            desc.baseColorFactor = toColor(baseColor);
        }

        float roughness = 0.5f;
        if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
            desc.roughness = roughness;
        }

        float metallic = 0.0f;
        if (source->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
            desc.metallic = metallic;
        }

        int twoSided = 0;
        if (source->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            desc.doubleSided = twoSided != 0;
        }

        desc.alphaMode = alphaModeFromAssimp(*source);

        // sRGB 由 material slot 决定：albedo 类 → sRGB；normal/metallic → linear（数据贴图）
        desc.baseColorTextureSrgb = true;
        desc.normalTextureSrgb = false;
        desc.metallicRoughnessTextureSrgb = false;

        if (options.importTextures) {
            desc.baseColorTexture = importTexture(*source, aiTextureType_BASE_COLOR, builder, baseDirectory);
            if (!desc.baseColorTexture.valid()) {
                desc.baseColorTexture = importTexture(*source, aiTextureType_DIFFUSE, builder, baseDirectory);
            }
            desc.normalTexture = importTexture(*source, aiTextureType_NORMALS, builder, baseDirectory);
            desc.metallicRoughnessTexture = importTexture(*source, aiTextureType_METALNESS, builder, baseDirectory);
        }

        materials[i] = builder.createMaterial(desc);
    }

    return materials;
}

graphics::Mesh buildMesh(const aiMesh& source) {
    std::vector<math::FVec3> positions;
    std::vector<math::FVec3> normals;
    std::vector<math::FVec2> texcoords;
    std::vector<uint32_t> indices;

    positions.reserve(source.mNumVertices);
    normals.reserve(source.mNumVertices);
    texcoords.reserve(source.mNumVertices);

    for (size_t i = 0; i < source.mNumVertices; ++i) {
        positions.push_back(toFVec3(source.mVertices[i]));
        normals.push_back(source.HasNormals() ? toFVec3(source.mNormals[i]) : math::FVec3(0.0f, 0.0f, 1.0f));
        texcoords.push_back(source.HasTextureCoords(0) ? toTexcoord(source.mTextureCoords[0][i]) : math::FVec2(0.0f));
    }

    indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);
    for (size_t i = 0; i < source.mNumFaces; ++i) {
        const aiFace& face = source.mFaces[i];
        if (face.mNumIndices != 3)
            continue;

        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.empty())
        return {};

    return buildStandardMesh(StandardMeshSource{
            .positions = std::span<const math::FVec3>{ positions },
            .normals = std::span<const math::FVec3>{ normals },
            .texcoords = std::span<const math::FVec2>{ texcoords },
            .indices = std::span<const uint32_t>{ indices },
            .topology = graphics::PrimitiveTopology::TriangleList,
    });
}

struct ImportedMeshRecord {
    ImportedMeshAsset asset;
    std::string name;
};

std::vector<std::optional<ImportedMeshRecord>> importMeshAssets(const aiScene& scene, ImportBuilder& builder,
                                                                std::span<const asset::AssetId> materials,
                                                                ImportReport& report) {
    std::vector<std::optional<ImportedMeshRecord>> records(scene.mNumMeshes);

    for (size_t i = 0; i < scene.mNumMeshes; ++i) {
        const aiMesh* source = scene.mMeshes[i];
        if (!source || !source->HasPositions())
            continue;

        graphics::Mesh mesh = buildMesh(*source);
        if (mesh.empty()) {
            report.warnings.push_back("Skipped empty imported mesh: " + std::to_string(i));
            continue;
        }

        asset::AssetId material = asset::AssetId::invalid();
        if (source->mMaterialIndex < materials.size()) {
            material = materials[source->mMaterialIndex];
        }

        std::string name = source->mName.length > 0 ? source->mName.C_Str() : "Mesh_" + std::to_string(i);
        builder.addPrimitive(std::move(mesh), material, std::move(name));

        auto asset = builder.commitAsset(source->mName.length > 0 ? source->mName.C_Str()
                                                                  : "MeshAsset_" + std::to_string(i));
        if (!asset) {
            report.warnings.push_back("Failed to create imported mesh asset: " + std::to_string(i));
            continue;
        }

        records[i] = ImportedMeshRecord{ std::move(*asset), source->mName.length > 0 ? source->mName.C_Str()
                                                                                     : "Mesh_" + std::to_string(i) };
    }

    return records;
}

scene::EntityId createNodeEntity(io::Document& doc, const std::string& name, scene::EntityId parent,
                                 const math::Mat4& local, const math::Mat4& world, ImportResult& result) {
    auto* scene = doc.scene();
    if (!scene)
        return scene::EntityId::invalid();

    scene::EntityId entity = scene->createEntity(name);
    if (parent)
        scene->setParent(entity, parent);
    scene->setLocalTransform(entity, local);
    scene->setWorldTransform(entity, world);
    result.entities.push_back(entity);
    return entity;
}

void applyMeshToEntity(io::Document& doc, scene::EntityId entity, const ImportedMeshRecord& mesh,
                       const math::Mat4& world) {
    auto* scene = doc.scene();
    if (!scene || !entity)
        return;

    scene->setGeometry(entity, mesh.asset.geometry);
    scene->setMaterialSlots(entity, mesh.asset.materialSlots);
    scene->setWorldBounds(entity, transformBounds(mesh.asset.bounds, world));
}

void importNodeRecursive(const aiNode& node, io::Document& doc,
                         std::span<const std::optional<ImportedMeshRecord>> meshes, scene::EntityId parent,
                         const math::Mat4& parentWorld, ImportResult& result) {
    const std::string nodeName = node.mName.length > 0 ? node.mName.C_Str() : "Node";
    const math::Mat4 local = toMat4(node.mTransformation);
    const math::Mat4 world = parentWorld * local;

    scene::EntityId nodeEntity = createNodeEntity(doc, nodeName, parent, local, world, result);

    if (node.mNumMeshes == 1) {
        const unsigned int meshIndex = node.mMeshes[0];
        if (meshIndex < meshes.size() && meshes[meshIndex]) {
            applyMeshToEntity(doc, nodeEntity, *meshes[meshIndex], world);
        } else {
            result.report.warnings.push_back("Node references missing mesh: " + nodeName);
        }
    } else {
        for (size_t i = 0; i < node.mNumMeshes; ++i) {
            const unsigned int meshIndex = node.mMeshes[i];
            if (meshIndex >= meshes.size() || !meshes[meshIndex]) {
                result.report.warnings.push_back("Node references missing mesh: " + nodeName);
                continue;
            }

            const ImportedMeshRecord& mesh = *meshes[meshIndex];
            scene::EntityId meshEntity = createNodeEntity(doc, mesh.name, nodeEntity, math::Mat4{ 1.0 }, world, result);
            applyMeshToEntity(doc, meshEntity, mesh, world);
        }
    }

    for (size_t i = 0; i < node.mNumChildren; ++i) {
        if (node.mChildren[i]) {
            importNodeRecursive(*node.mChildren[i], doc, meshes, nodeEntity, world, result);
        }
    }
}

}  // namespace

core::Result<ImportResult> AssimpImporter::import(const std::string& path, mulan::io::Document& doc,
                                                  const ImportOptions& options) {
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                               aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials |
                               aiProcess_FindInvalidData | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
                               aiProcess_SortByPType |
                               (options.generateMissingNormals ? aiProcess_GenSmoothNormals : 0u);

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        return std::unexpected(core::Error::make(
                core::ErrorCode::Io, std::string("Assimp failed to import model: ") + importer.GetErrorString()));
    }

    ImportBuilder builder(doc);
    const std::filesystem::path baseDirectory = std::filesystem::path(path).parent_path();
    std::vector<asset::AssetId> materials = importMaterials(*scene, builder, baseDirectory, options);

    ImportResult result;
    ImportReport importReport;
    auto meshes = importMeshAssets(*scene, builder, materials, importReport);
    const bool hasMeshAsset =
            std::any_of(meshes.begin(), meshes.end(), [](const auto& mesh) { return mesh.has_value(); });
    if (!hasMeshAsset) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Imported model contains no renderable meshes"));
    }

    if (options.flattenNodeHierarchy) {
        importReport.warnings.push_back("Assimp import currently preserves node hierarchy; flattening is not applied");
    }

    const double unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;
    if (options.unitScale <= 0.0) {
        importReport.warnings.push_back("Invalid import unit scale; using 1.0");
    }

    const math::Mat4 rootWorld = math::Mat4::scale(math::Vec3(unitScale));
    importNodeRecursive(*scene->mRootNode, doc, meshes, scene::EntityId::invalid(), rootWorld, result);
    auto nodeWarnings = std::move(result.report.warnings);

    result.report = builder.report();
    result.report.entityCount = result.entities.size();
    result.report.warnings.insert(result.report.warnings.end(), importReport.warnings.begin(),
                                  importReport.warnings.end());
    result.report.warnings.insert(result.report.warnings.end(), nodeWarnings.begin(), nodeWarnings.end());
    return result;
}

std::vector<std::string> AssimpImporter::supportedExtensions() const {
    return { kSupportedExtensions.begin(), kSupportedExtensions.end() };
}

std::string AssimpImporter::name() const {
    return "Assimp Importer";
}

}  // namespace mulan::io
