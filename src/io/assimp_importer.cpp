#include "assimp_importer.h"
#include "import_builder.h"  // buildStandardMesh / StandardMeshSource
#include "parsed_scene.h"

#include <mulan/core/result/error.h>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

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

std::string materialName(const aiMaterial& material, size_t index) {
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0)
        return name.C_Str();
    return "Material_" + std::to_string(index);
}

graphics::AlphaMode alphaModeFromAssimp(const aiMaterial& material) {
#ifdef AI_MATKEY_GLTF_ALPHAMODE
    aiString blendMode;
    if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, blendMode) == AI_SUCCESS) {
        const std::string value = blendMode.C_Str();
        if (value == "BLEND")
            return graphics::AlphaMode::Blend;
        if (value == "MASK")
            return graphics::AlphaMode::Mask;
    }
#else
    (void) material;
#endif
    return graphics::AlphaMode::Opaque;
}

// ============================================================
// 纹理:产出 ParsedTexture,返回 ParsedScene.textures 索引(SIZE_MAX = 失败)
// ============================================================
size_t importTexture(const aiScene& scene, aiMaterial& material, aiTextureType type, ParsedScene& parsed,
                     const std::filesystem::path& baseDirectory) {
    if (material.GetTextureCount(type) == 0)
        return SIZE_MAX;

    aiString path;
    if (material.GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0)
        return SIZE_MAX;

    ParsedTexture desc;
    std::filesystem::path texturePath(path.C_Str());
    const std::string texturePathString = texturePath.string();
    if (!texturePathString.empty() && texturePathString.front() == '*') {
        desc.name = "EmbeddedTexture_" + texturePathString.substr(1);
        desc.sourcePath = "assimp:embedded#" + texturePathString;

        size_t textureIndex = 0;
        try {
            textureIndex = static_cast<size_t>(std::stoull(texturePathString.substr(1)));
        } catch (...) {
            return SIZE_MAX;
        }
        if (textureIndex >= scene.mNumTextures || !scene.mTextures[textureIndex])
            return SIZE_MAX;

        const aiTexture& embedded = *scene.mTextures[textureIndex];
        if (!embedded.pcData || embedded.mWidth == 0 || embedded.mHeight != 0)
            return SIZE_MAX;

        const auto* begin = reinterpret_cast<const std::byte*>(embedded.pcData);
        desc.data.assign(begin, begin + embedded.mWidth);
        if (embedded.achFormatHint[0] != '\0')
            desc.mimeType = std::string("image/") + embedded.achFormatHint;
    } else {
        desc.name = texturePath.filename().string();
        desc.sourcePath = texturePath.is_relative() ? (baseDirectory / texturePath).string() : texturePath.string();
    }

    size_t idx = parsed.textures.size();
    parsed.textures.push_back(std::move(desc));
    return idx;
}

// ============================================================
// 材质:产出 ParsedMaterial,纹理用 ParsedScene.textures 索引
// ============================================================
std::vector<size_t> importMaterials(const aiScene& scene, ParsedScene& parsed,
                                    const std::filesystem::path& baseDirectory, const ImportOptions& options) {
    std::vector<size_t> materials(scene.mNumMaterials, SIZE_MAX);
    if (!options.importMaterials)
        return materials;

    for (size_t i = 0; i < scene.mNumMaterials; ++i) {
        aiMaterial* source = scene.mMaterials[i];
        if (!source)
            continue;

        ParsedMaterial desc;
        desc.name = materialName(*source, i);

        aiColor4D baseColor;
        if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS ||
            aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS) {
            desc.baseColorFactor = toColor(baseColor);
        }

        float roughness = 0.5f;
        if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
            desc.roughness = roughness;

        float metallic = 0.0f;
        if (source->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
            desc.metallic = metallic;

        int twoSided = 0;
        if (source->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS)
            desc.doubleSided = twoSided != 0;

        desc.alphaMode = alphaModeFromAssimp(*source);
        desc.baseColorTextureSrgb = true;
        desc.normalTextureSrgb = false;
        desc.metallicRoughnessTextureSrgb = false;

        if (options.importTextures) {
            desc.baseColorTexture = importTexture(scene, *source, aiTextureType_BASE_COLOR, parsed, baseDirectory);
            if (desc.baseColorTexture == SIZE_MAX)
                desc.baseColorTexture = importTexture(scene, *source, aiTextureType_DIFFUSE, parsed, baseDirectory);
            desc.normalTexture = importTexture(scene, *source, aiTextureType_NORMALS, parsed, baseDirectory);
            desc.metallicRoughnessTexture =
                    importTexture(scene, *source, aiTextureType_METALNESS, parsed, baseDirectory);
        }

        size_t idx = parsed.materials.size();
        parsed.materials.push_back(std::move(desc));
        materials[i] = idx;
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

// ============================================================
// 网格:每个 aiMesh → ParsedMesh,材质用 materials 索引
// ============================================================
std::vector<size_t> importMeshAssets(const aiScene& scene, ParsedScene& parsed, const std::vector<size_t>& materials) {
    std::vector<size_t> indices(scene.mNumMeshes, SIZE_MAX);

    for (size_t i = 0; i < scene.mNumMeshes; ++i) {
        const aiMesh* source = scene.mMeshes[i];
        if (!source || !source->HasPositions())
            continue;

        graphics::Mesh mesh = buildMesh(*source);
        if (mesh.empty())
            continue;

        std::string name = source->mName.length > 0 ? source->mName.C_Str() : "Mesh_" + std::to_string(i);

        ParsedMesh pm;
        pm.name = name;
        asset::MeshPrimitive prim;
        prim.mesh = std::move(mesh);
        pm.primitives.push_back(std::move(prim));

        size_t matIdx = SIZE_MAX;
        if (source->mMaterialIndex < materials.size())
            matIdx = materials[source->mMaterialIndex];
        pm.materialIndices.push_back(matIdx);

        indices[i] = parsed.meshes.size();
        parsed.meshes.push_back(std::move(pm));
    }
    return indices;
}

// ============================================================
// 节点树 → ParsedNode(不碰 Document)
// 多 mesh 节点:每个 mesh 建一个子节点。
// ============================================================
void importNode(const aiNode& node, ParsedScene& parsed, const std::vector<size_t>& meshes, size_t parentIdx) {
    const std::string nodeName = node.mName.length > 0 ? node.mName.C_Str() : "Node";
    const math::Mat4 local = toMat4(node.mTransformation);

    if (node.mNumMeshes <= 1) {
        ParsedNode pn;
        pn.name = nodeName;
        pn.parent = parentIdx;
        pn.localTransform = local;
        if (node.mNumMeshes == 1) {
            const unsigned int meshIndex = node.mMeshes[0];
            if (meshIndex < meshes.size() && meshes[meshIndex] != SIZE_MAX)
                pn.meshIndex = meshes[meshIndex];
        }
        size_t selfIdx = parsed.nodes.size();
        parsed.nodes.push_back(std::move(pn));

        for (size_t i = 0; i < node.mNumChildren; ++i) {
            if (node.mChildren[i])
                importNode(*node.mChildren[i], parsed, meshes, selfIdx);
        }
    } else {
        // 多 mesh 节点:先建容器节点,每个 mesh 一个子节点
        ParsedNode container;
        container.name = nodeName;
        container.parent = parentIdx;
        container.localTransform = local;
        size_t containerIdx = parsed.nodes.size();
        parsed.nodes.push_back(std::move(container));

        for (size_t i = 0; i < node.mNumMeshes; ++i) {
            const unsigned int meshIndex = node.mMeshes[i];
            if (meshIndex >= meshes.size() || meshes[meshIndex] == SIZE_MAX)
                continue;
            ParsedNode meshNode;
            const auto& pm = parsed.meshes[meshes[meshIndex]];
            meshNode.name = pm.name;
            meshNode.parent = containerIdx;
            meshNode.localTransform = math::Mat4{ 1.0 };
            meshNode.meshIndex = meshes[meshIndex];
            parsed.nodes.push_back(std::move(meshNode));
        }

        for (size_t i = 0; i < node.mNumChildren; ++i) {
            if (node.mChildren[i])
                importNode(*node.mChildren[i], parsed, meshes, containerIdx);
        }
    }
}

}  // namespace

// ============================================================
// AssimpImporter
// ============================================================

core::Result<ParsedScene> AssimpImporter::parse(const std::string& path, const ImportOptions& options) {
    ParsedScene scene;
    scene.unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;

    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                               aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials |
                               aiProcess_FindInvalidData | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
                               aiProcess_SortByPType |
                               (options.generateMissingNormals ? aiProcess_GenSmoothNormals : 0u);

    const aiScene* aiScene = importer.ReadFile(path, flags);
    if (!aiScene || !aiScene->mRootNode) {
        return std::unexpected(core::Error::make(
                core::ErrorCode::Io, std::string("Assimp failed to import model: ") + importer.GetErrorString()));
    }

    const std::filesystem::path baseDirectory = std::filesystem::path(path).parent_path();
    auto materials = importMaterials(*aiScene, scene, baseDirectory, options);
    auto meshes = importMeshAssets(*aiScene, scene, materials);

    bool hasMesh = std::any_of(meshes.begin(), meshes.end(), [](size_t idx) { return idx != SIZE_MAX; });
    if (!hasMesh) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "Imported model contains no renderable meshes"));
    }

    importNode(*aiScene->mRootNode, scene, meshes, SIZE_MAX);
    scene.rootNodes.push_back(0);  // 根节点是第一个(index 0)

    return scene;
}

std::vector<std::string> AssimpImporter::supportedExtensions() const {
    return { kSupportedExtensions.begin(), kSupportedExtensions.end() };
}

std::string AssimpImporter::name() const {
    return "Assimp Importer";
}

}  // namespace mulan::io
