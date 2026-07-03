#include "assimp_importer.h"
#include "importer_factory.h"
#include "mesh_import_builder.h"

#include <mulan/core/result/error.h>
#include <mulan/document/document.h>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mulan::io {
namespace {

constexpr std::array<const char*, 14> kSupportedExtensions = {
    "obj", "fbx", "dae", "3ds", "ply", "stl", "gltf", "glb",
    "blend", "x", "ase", "lwo", "off", "dxf"
};

engine::FVec3 toFVec3(const aiVector3D& value) {
    return {value.x, value.y, value.z};
}

glm::vec2 toTexcoord(const aiVector3D& value) {
    return {value.x, value.y};
}

engine::Vec4 toColor(const aiColor4D& value) {
    return {value.r, value.g, value.b, value.a};
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
        if (value == "BLEND") return asset::AlphaMode::Blend;
        if (value == "MASK") return asset::AlphaMode::Mask;
    }
#else
    (void)material;
#endif
    return asset::AlphaMode::Opaque;
}

asset::AssetId importTexture(aiMaterial& material,
                             aiTextureType type,
                             MeshImportBuilder& builder,
                             const std::filesystem::path& baseDirectory,
                             bool srgb) {
    if (material.GetTextureCount(type) == 0) return asset::AssetId::invalid();

    aiString path;
    if (material.GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0) {
        return asset::AssetId::invalid();
    }

    ImportedTextureDesc desc;
    std::filesystem::path texturePath(path.C_Str());
    desc.name = texturePath.filename().string();
    desc.sourcePath = texturePath.is_relative()
        ? (baseDirectory / texturePath).string()
        : texturePath.string();
    desc.srgb = srgb;
    return builder.createTexture(desc);
}

std::vector<asset::AssetId> importMaterials(const aiScene& scene,
                                            MeshImportBuilder& builder,
                                            const std::filesystem::path& baseDirectory,
                                            const ImportOptions& options) {
    std::vector<asset::AssetId> materials(scene.mNumMaterials, asset::AssetId::invalid());
    if (!options.importMaterials) return materials;

    for (size_t i = 0; i < scene.mNumMaterials; ++i) {
        aiMaterial* source = scene.mMaterials[i];
        if (!source) continue;

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

        if (options.importTextures) {
            desc.baseColorTexture = importTexture(*source, aiTextureType_BASE_COLOR, builder,
                                                  baseDirectory, true);
            if (!desc.baseColorTexture.valid()) {
                desc.baseColorTexture = importTexture(*source, aiTextureType_DIFFUSE, builder,
                                                      baseDirectory, true);
            }
            desc.normalTexture = importTexture(*source, aiTextureType_NORMALS, builder,
                                               baseDirectory, false);
            desc.metallicRoughnessTexture =
                importTexture(*source, aiTextureType_METALNESS, builder, baseDirectory, false);
        }

        materials[i] = builder.createMaterial(desc);
    }

    return materials;
}

engine::Mesh buildMesh(const aiMesh& source) {
    std::vector<engine::FVec3> positions;
    std::vector<engine::FVec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<uint32_t> indices;

    positions.reserve(source.mNumVertices);
    normals.reserve(source.mNumVertices);
    texcoords.reserve(source.mNumVertices);

    for (size_t i = 0; i < source.mNumVertices; ++i) {
        positions.push_back(toFVec3(source.mVertices[i]));
        normals.push_back(source.HasNormals()
            ? toFVec3(source.mNormals[i])
            : engine::FVec3(0.0f, 0.0f, 1.0f));
        texcoords.push_back(source.HasTextureCoords(0)
            ? toTexcoord(source.mTextureCoords[0][i])
            : glm::vec2(0.0f));
    }

    indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);
    for (size_t i = 0; i < source.mNumFaces; ++i) {
        const aiFace& face = source.mFaces[i];
        if (face.mNumIndices != 3) continue;

        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.empty()) return {};

    return buildStandardMesh(StandardMeshSource{
        .positions = std::span<const engine::FVec3>{positions},
        .normals = std::span<const engine::FVec3>{normals},
        .texcoords = std::span<const glm::vec2>{texcoords},
        .indices = std::span<const uint32_t>{indices},
        .topology = engine::PrimitiveTopology::TriangleList,
    });
}

void appendSceneMeshes(const aiScene& scene,
                       MeshImportBuilder& builder,
                       std::span<const asset::AssetId> materials,
                       ImportReport& report) {
    for (size_t i = 0; i < scene.mNumMeshes; ++i) {
        const aiMesh* source = scene.mMeshes[i];
        if (!source || !source->HasPositions()) continue;

        engine::Mesh mesh = buildMesh(*source);
        if (mesh.empty()) {
            report.warnings.push_back("Skipped empty imported mesh: " + std::to_string(i));
            continue;
        }

        asset::AssetId material = asset::AssetId::invalid();
        if (source->mMaterialIndex < materials.size()) {
            material = materials[source->mMaterialIndex];
        }

        std::string name = source->mName.length > 0
            ? source->mName.C_Str()
            : "Mesh_" + std::to_string(i);
        builder.addPrimitive(std::move(mesh), material, std::move(name));
    }
}

} // namespace

std::expected<ImportResult, core::Error>
AssimpImporter::import(const std::string& path,
                       mulan::document::Document& doc,
                       const ImportOptions& options) {
    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_RemoveRedundantMaterials |
        aiProcess_FindInvalidData |
        aiProcess_GenUVCoords |
        aiProcess_TransformUVCoords |
        aiProcess_SortByPType |
        (options.generateMissingNormals ? aiProcess_GenSmoothNormals : 0u);

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        return std::unexpected(core::Error::make(
            core::ErrorCode::Io,
            std::string("Assimp failed to import model: ") + importer.GetErrorString()));
    }

    MeshImportBuilder builder(doc);
    ImportReport report;
    const std::filesystem::path baseDirectory = std::filesystem::path(path).parent_path();
    std::vector<asset::AssetId> materials = importMaterials(*scene, builder, baseDirectory, options);
    appendSceneMeshes(*scene, builder, materials, report);

    const std::string modelName = std::filesystem::path(path).stem().string();
    auto entity = builder.commit(modelName.empty() ? "Imported Model" : modelName);
    if (!entity) {
        return std::unexpected(entity.error());
    }

    ImportResult result;
    result.entities.push_back(*entity);
    result.report = builder.report();
    result.report.warnings.insert(result.report.warnings.end(),
                                  report.warnings.begin(),
                                  report.warnings.end());
    return result;
}

std::vector<std::string> AssimpImporter::supportedExtensions() const {
    return {kSupportedExtensions.begin(), kSupportedExtensions.end()};
}

std::string AssimpImporter::name() const {
    return "Assimp Importer";
}

static AutoRegisterImporter _reg_obj("obj", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_fbx("fbx", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_dae("dae", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_3ds("3ds", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_ply("ply", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_stl("stl", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_gltf("gltf", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_glb("glb", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_blend("blend", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_x("x", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_ase("ase", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_lwo("lwo", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_off("off", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});
static AutoRegisterImporter _reg_dxf("dxf", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<AssimpImporter>();
});

} // namespace mulan::io
