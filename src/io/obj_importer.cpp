#include "obj_importer.h"

#include "detail/mesh_attribute_generator.h"
#include "import_builder.h"
#include "import_path_utils.h"

#include <mulan/core/profiling/profile.h>
#include <mulan/core/result/error.h>

#include <rapidobj/rapidobj.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::io {
namespace {

constexpr size_t kRapidObjMaxLineLength = 4u * 1024u;

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

Result<std::filesystem::path> validateSourcePath(const std::string& path, uint64_t maxFileBytes) {
    std::error_code ec;
    const std::filesystem::path source = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::is_regular_file(source, ec) || ec) {
        return std::unexpected(Error::make(ErrorCode::Io, "OBJ file does not exist or is not a regular file: " + path));
    }

    const uint64_t fileSize = std::filesystem::file_size(source, ec);
    if (ec) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to query OBJ file size: " + path));
    }
    if (fileSize > maxFileBytes) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ file exceeds the configured import limit"));
    }
    return source;
}

Result<std::optional<std::filesystem::path>> findMaterialLibrary(const std::filesystem::path& sourcePath,
                                                                 uint64_t maxFileBytes) {
    std::ifstream stream(sourcePath, std::ios::binary);
    if (!stream) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to open OBJ file: " + sourcePath.string()));
    }

    std::array<char, kRapidObjMaxLineLength + 1u> lineBuffer{};
    std::optional<std::filesystem::path> materialPath;
    std::string materialReference;
    size_t lineNumber = 0;

    while (stream.getline(lineBuffer.data(), static_cast<std::streamsize>(lineBuffer.size()))) {
        ++lineNumber;
        std::string_view line(lineBuffer.data(), static_cast<size_t>(stream.gcount()));
        if (!line.empty() && line.back() == '\0')
            line.remove_suffix(1);
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;

        constexpr std::string_view directive = "mtllib";
        if (!line.starts_with(directive) ||
            (line.size() > directive.size() && line[directive.size()] != ' ' && line[directive.size()] != '\t')) {
            continue;
        }

        const std::string_view reference = trim(line.substr(directive.size()));
        if (reference.empty()) {
            return std::unexpected(
                    Error::make(ErrorCode::InvalidArg,
                                "OBJ material library directive is empty at line " + std::to_string(lineNumber)));
        }
        if (!materialReference.empty() && materialReference != reference) {
            return std::unexpected(Error::make(
                    ErrorCode::NotSupported,
                    "OBJ contains multiple material libraries; RapidObj 1.1 supports one material library"));
        }
        if (materialReference.empty()) {
            materialReference.assign(reference);
            materialPath = detail::resolveContainedImportPath(sourcePath.parent_path(),
                                                              std::filesystem::path(materialReference));
            if (!materialPath) {
                return std::unexpected(
                        Error::make(ErrorCode::InvalidArg, "OBJ material library escapes the model directory"));
            }
            if (!detail::importFileWithinLimit(*materialPath, maxFileBytes)) {
                return std::unexpected(
                        Error::make(ErrorCode::InvalidArg, "OBJ material library exceeds the configured import limit"));
            }
        }
    }

    if (stream.bad()) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed while scanning OBJ material library directive"));
    }
    if (stream.fail() && !stream.eof()) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ contains a line longer than RapidObj supports"));
    }
    return materialPath;
}

std::string rapidObjErrorMessage(const rapidobj::Error& error) {
    std::string message = "RapidObj failed to parse OBJ: " + error.code.message();
    if (error.line_num != 0)
        message += " (line " + std::to_string(error.line_num) + ")";
    if (!error.line.empty()) {
        constexpr size_t kMaxDiagnosticLineLength = 256;
        message += ": " + error.line.substr(0, kMaxDiagnosticLineLength);
    }
    return message;
}

bool addByteCount(size_t count, size_t elementSize, uint64_t& total) {
    if (count > std::numeric_limits<uint64_t>::max() / elementSize)
        return false;
    const uint64_t bytes = static_cast<uint64_t>(count) * elementSize;
    if (total > std::numeric_limits<uint64_t>::max() - bytes)
        return false;
    total += bytes;
    return true;
}

ResultVoid validateParsedData(const rapidobj::Result& source, const ImportOptions& options) {
    const auto& attributes = source.attributes;
    if (attributes.positions.empty() || attributes.positions.size() % 3u != 0u ||
        attributes.normals.size() % 3u != 0u || attributes.texcoords.size() % 2u != 0u) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ contains malformed vertex attribute arrays"));
    }

    const size_t positionCount = attributes.positions.size() / 3u;
    const size_t normalCount = attributes.normals.size() / 3u;
    const size_t texcoordCount = attributes.texcoords.size() / 2u;
    if (positionCount > options.maxAccessorElements || normalCount > options.maxAccessorElements ||
        texcoordCount > options.maxAccessorElements) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ vertex attribute count exceeds import limits"));
    }

    uint64_t totalBytes = 0;
    if (!addByteCount(attributes.positions.size(), sizeof(float), totalBytes) ||
        !addByteCount(attributes.normals.size(), sizeof(float), totalBytes) ||
        !addByteCount(attributes.texcoords.size(), sizeof(float), totalBytes) ||
        totalBytes > options.maxTotalAccessorBytes) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ vertex data exceeds the configured byte limit"));
    }
    return {};
}

class TextureCollector {
public:
    TextureCollector(ParsedScene& scene, std::filesystem::path modelDirectory, std::filesystem::path materialDirectory,
                     uint64_t maxFileBytes)
        : scene_(scene),
          model_directory_(std::move(modelDirectory)),
          material_directory_(std::move(materialDirectory)),
          max_file_bytes_(maxFileBytes) {}

    Result<size_t> add(std::string_view textureName) {
        if (textureName.empty())
            return SIZE_MAX;

        const std::filesystem::path textureReference(textureName);
        if (textureReference.empty() || textureReference.is_absolute() || textureReference.has_root_name() ||
            textureReference.has_root_directory()) {
            return std::unexpected(Error::make(
                    ErrorCode::InvalidArg, "OBJ texture uses an invalid absolute path: " + std::string(textureName)));
        }

        const std::filesystem::path materialRelative = material_directory_.lexically_relative(model_directory_);
        if (materialRelative.empty() || materialRelative.is_absolute()) {
            return std::unexpected(Error::make(ErrorCode::Internal, "OBJ material directory is outside model root"));
        }
        const auto resolved = detail::resolveContainedImportPath(model_directory_, materialRelative / textureReference);
        if (!resolved) {
            return std::unexpected(Error::make(ErrorCode::InvalidArg,
                                               "OBJ texture escapes the model directory: " + std::string(textureName)));
        }
        if (!detail::importFileWithinLimit(*resolved, max_file_bytes_)) {
            return std::unexpected(
                    Error::make(ErrorCode::InvalidArg,
                                "OBJ texture exceeds the configured import limit: " + std::string(textureName)));
        }

        const std::string key = resolved->generic_string();
        if (const auto known = indices_.find(key); known != indices_.end())
            return known->second;

        ParsedTexture texture;
        texture.name = resolved->filename().string();
        texture.sourcePath = resolved->string();
        const size_t index = scene_.textures.size();
        scene_.textures.push_back(std::move(texture));
        indices_.emplace(key, index);
        return index;
    }

private:
    ParsedScene& scene_;
    std::filesystem::path model_directory_;
    std::filesystem::path material_directory_;
    uint64_t max_file_bytes_ = 0;
    std::unordered_map<std::string, size_t> indices_;
};

double clampUnit(float value, double fallback = 0.0) {
    if (!std::isfinite(value))
        return fallback;
    return static_cast<double>(std::clamp(value, 0.0f, 1.0f));
}

Result<std::vector<size_t>> importMaterials(const rapidobj::Result& source, ParsedScene& scene,
                                            const std::filesystem::path& modelDirectory,
                                            const std::filesystem::path& materialDirectory,
                                            const ImportOptions& options) {
    std::vector<size_t> indices(source.materials.size(), SIZE_MAX);
    if (!options.importMaterials)
        return indices;

    TextureCollector textures(scene, modelDirectory, materialDirectory, options.maxExternalFileBytes);
    for (size_t i = 0; i < source.materials.size(); ++i) {
        const rapidobj::Material& material = source.materials[i];
        ParsedMaterial parsed;
        parsed.name = material.name.empty() ? "Material_" + std::to_string(i) : material.name;
        parsed.baseColorFactor = math::Vec4(clampUnit(material.diffuse[0]), clampUnit(material.diffuse[1]),
                                            clampUnit(material.diffuse[2]), clampUnit(material.dissolve, 1.0));
        parsed.roughness = material.roughness > 0.0f ? clampUnit(material.roughness) : 0.5;
        parsed.metallic = clampUnit(material.metallic);
        parsed.emissiveFactor = math::Vec3(clampUnit(material.emission[0]), clampUnit(material.emission[1]),
                                           clampUnit(material.emission[2]));
        parsed.alphaMode = material.dissolve < 0.999f ? graphics::AlphaMode::Blend : graphics::AlphaMode::Opaque;

        if (options.importTextures) {
            auto baseColor = textures.add(material.diffuse_texname);
            if (!baseColor)
                return std::unexpected(baseColor.error());
            parsed.baseColorTexture = *baseColor;

            auto normal = textures.add(material.normal_texname);
            if (!normal)
                return std::unexpected(normal.error());
            parsed.normalTexture = *normal;

            auto emissive = textures.add(material.emissive_texname);
            if (!emissive)
                return std::unexpected(emissive.error());
            parsed.emissiveTexture = *emissive;
        }

        indices[i] = scene.materials.size();
        scene.materials.push_back(std::move(parsed));
    }
    return indices;
}

math::FVec3 positionAt(const rapidobj::Attributes& attributes, int32_t index) {
    const size_t offset = static_cast<size_t>(index) * 3u;
    return math::FVec3(attributes.positions[offset], attributes.positions[offset + 1u],
                       attributes.positions[offset + 2u]);
}

math::FVec3 normalAt(const rapidobj::Attributes& attributes, int32_t index) {
    const size_t offset = static_cast<size_t>(index) * 3u;
    return math::FVec3(attributes.normals[offset], attributes.normals[offset + 1u], attributes.normals[offset + 2u]);
}

math::FVec2 texcoordAt(const rapidobj::Attributes& attributes, int32_t index) {
    const size_t offset = static_cast<size_t>(index) * 2u;
    return math::FVec2(attributes.texcoords[offset], attributes.texcoords[offset + 1u]);
}

uint64_t smoothNormalKey(int32_t positionIndex, uint32_t smoothingGroup) {
    return (static_cast<uint64_t>(smoothingGroup) << 32u) | static_cast<uint32_t>(positionIndex);
}

struct ShapeNormals {
    std::vector<math::FVec3> faceNormals;
    std::unordered_map<uint64_t, math::FVec3> smoothNormals;
};

Result<ShapeNormals> buildShapeNormals(const rapidobj::Attributes& attributes, const rapidobj::Mesh& mesh,
                                       bool generateMissingNormals) {
    const size_t faceCount = mesh.num_face_vertices.size();
    if (faceCount > std::numeric_limits<size_t>::max() / 3u || mesh.indices.size() != faceCount * 3u ||
        (!mesh.material_ids.empty() && mesh.material_ids.size() != faceCount) ||
        (!mesh.smoothing_group_ids.empty() && mesh.smoothing_group_ids.size() != faceCount)) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Triangulated OBJ mesh has inconsistent face data"));
    }

    const size_t positionCount = attributes.positions.size() / 3u;
    const size_t normalCount = attributes.normals.size() / 3u;
    const size_t texcoordCount = attributes.texcoords.size() / 2u;

    ShapeNormals output;
    output.faceNormals.resize(faceCount, math::FVec3::unitZ());
    for (size_t face = 0; face < faceCount; ++face) {
        const size_t firstCorner = face * 3u;
        std::array<math::FVec3, 3> positions{};
        for (size_t corner = 0; corner < 3u; ++corner) {
            const rapidobj::Index& index = mesh.indices[firstCorner + corner];
            if (index.position_index < 0 || static_cast<size_t>(index.position_index) >= positionCount ||
                (index.normal_index >= 0 && static_cast<size_t>(index.normal_index) >= normalCount) ||
                (index.texcoord_index >= 0 && static_cast<size_t>(index.texcoord_index) >= texcoordCount)) {
                return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ face contains an invalid vertex index"));
            }
            positions[corner] = positionAt(attributes, index.position_index);
        }

        const math::FVec3 weightedNormal = (positions[1] - positions[0]).cross(positions[2] - positions[0]);
        output.faceNormals[face] = weightedNormal.normalizedOr(math::FVec3::unitZ());

        const uint32_t smoothingGroup = mesh.smoothing_group_ids.empty() ? 0u : mesh.smoothing_group_ids[face];
        if (!generateMissingNormals || smoothingGroup == 0u)
            continue;
        for (size_t corner = 0; corner < 3u; ++corner) {
            const rapidobj::Index& index = mesh.indices[firstCorner + corner];
            if (index.normal_index < 0)
                output.smoothNormals[smoothNormalKey(index.position_index, smoothingGroup)] += weightedNormal;
        }
    }

    for (auto& [key, normal] : output.smoothNormals) {
        (void) key;
        normal = normal.normalizedOr(math::FVec3::unitZ());
    }
    return output;
}

enum class NormalSource : uint8_t {
    Explicit,
    GeneratedSmooth,
    GeneratedFlat,
    Default,
};

struct VertexKey {
    int32_t position = -1;
    int32_t texcoord = -1;
    int32_t normal = -1;
    uint32_t smoothingGroup = 0;
    uint64_t face = 0;
    NormalSource normalSource = NormalSource::Default;

    bool operator==(const VertexKey&) const = default;
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const noexcept {
        size_t value = std::hash<int32_t>{}(key.position);
        const auto combine = [&value](size_t part) {
            value ^= part + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
        };
        combine(std::hash<int32_t>{}(key.texcoord));
        combine(std::hash<int32_t>{}(key.normal));
        combine(std::hash<uint32_t>{}(key.smoothingGroup));
        combine(std::hash<uint64_t>{}(key.face));
        combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.normalSource)));
        return value;
    }
};

struct PrimitiveBuildState {
    int32_t sourceMaterial = -1;
    detail::TriangleMeshData geometry;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertices;
    bool hasCompleteTexcoords = true;
};

VertexKey vertexKey(const rapidobj::Index& index, size_t face, uint32_t smoothingGroup, bool generateMissingNormals) {
    VertexKey key;
    key.position = index.position_index;
    key.texcoord = index.texcoord_index;
    key.normal = index.normal_index;
    if (index.normal_index >= 0) {
        key.normalSource = NormalSource::Explicit;
    } else if (!generateMissingNormals) {
        key.normalSource = NormalSource::Default;
    } else if (smoothingGroup != 0u) {
        key.normalSource = NormalSource::GeneratedSmooth;
        key.smoothingGroup = smoothingGroup;
    } else {
        key.normalSource = NormalSource::GeneratedFlat;
        key.face = face;
    }
    return key;
}

math::FVec3 resolveNormal(const rapidobj::Attributes& attributes, const rapidobj::Index& index, size_t face,
                          uint32_t smoothingGroup, bool generateMissingNormals, const ShapeNormals& normals) {
    if (index.normal_index >= 0)
        return normalAt(attributes, index.normal_index).normalizedOr(normals.faceNormals[face]);
    if (!generateMissingNormals)
        return math::FVec3::unitZ();
    if (smoothingGroup == 0u)
        return normals.faceNormals[face];
    if (const auto known = normals.smoothNormals.find(smoothNormalKey(index.position_index, smoothingGroup));
        known != normals.smoothNormals.end()) {
        return known->second;
    }
    return normals.faceNormals[face];
}

Result<uint32_t> appendVertex(PrimitiveBuildState& output, const VertexKey& key, const rapidobj::Index& index,
                              size_t face, uint32_t smoothingGroup, bool generateMissingNormals,
                              const rapidobj::Attributes& attributes, const ShapeNormals& normals,
                              size_t maxVertexCount) {
    if (const auto known = output.vertices.find(key); known != output.vertices.end())
        return known->second;
    if (output.geometry.positions.size() >= maxVertexCount ||
        output.geometry.positions.size() >= std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ mesh exceeds the configured vertex limit"));
    }

    const uint32_t vertex = static_cast<uint32_t>(output.geometry.positions.size());
    output.geometry.positions.push_back(positionAt(attributes, index.position_index));
    output.geometry.normals.push_back(
            resolveNormal(attributes, index, face, smoothingGroup, generateMissingNormals, normals));
    if (index.texcoord_index >= 0) {
        output.geometry.texcoords.push_back(texcoordAt(attributes, index.texcoord_index));
    } else {
        output.geometry.texcoords.push_back(math::FVec2(0.0f));
        output.hasCompleteTexcoords = false;
    }
    output.vertices.emplace(key, vertex);
    return vertex;
}

bool materialUsesNormalTexture(const ParsedScene& scene, const std::vector<size_t>& materialIndices,
                               int32_t sourceMaterial) {
    if (sourceMaterial < 0 || static_cast<size_t>(sourceMaterial) >= materialIndices.size())
        return false;
    const size_t parsedMaterial = materialIndices[static_cast<size_t>(sourceMaterial)];
    return parsedMaterial < scene.materials.size() && scene.materials[parsedMaterial].normalTexture != SIZE_MAX;
}

Result<std::optional<ParsedMesh>> buildParsedMesh(const rapidobj::Result& source, const rapidobj::Shape& shape,
                                                  size_t shapeIndex, const std::vector<size_t>& materialIndices,
                                                  const ImportOptions& options, const ParsedScene& scene,
                                                  uint64_t& totalOutputBytes) {
    if (shape.mesh.indices.empty())
        return std::optional<ParsedMesh>{};
    if (shape.mesh.indices.size() > options.maxAccessorElements) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ face index count exceeds import limits"));
    }

    auto shapeNormals = buildShapeNormals(source.attributes, shape.mesh, options.generateMissingNormals);
    if (!shapeNormals)
        return std::unexpected(shapeNormals.error());

    std::vector<PrimitiveBuildState> builders;
    std::unordered_map<int32_t, size_t> builderByMaterial;
    const size_t faceCount = shape.mesh.num_face_vertices.size();
    for (size_t face = 0; face < faceCount; ++face) {
        const int32_t material = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[face];
        auto [known, inserted] = builderByMaterial.try_emplace(material, builders.size());
        if (inserted) {
            PrimitiveBuildState builder;
            builder.sourceMaterial = material;
            builders.push_back(std::move(builder));
        }
        PrimitiveBuildState& builder = builders[known->second];
        const uint32_t smoothingGroup =
                shape.mesh.smoothing_group_ids.empty() ? 0u : shape.mesh.smoothing_group_ids[face];

        for (size_t corner = 0; corner < 3u; ++corner) {
            const rapidobj::Index& index = shape.mesh.indices[face * 3u + corner];
            const VertexKey key = vertexKey(index, face, smoothingGroup, options.generateMissingNormals);
            auto vertex = appendVertex(builder, key, index, face, smoothingGroup, options.generateMissingNormals,
                                       source.attributes, *shapeNormals, options.maxAccessorElements);
            if (!vertex)
                return std::unexpected(vertex.error());
            builder.geometry.indices.push_back(*vertex);
        }
    }

    ParsedMesh parsed;
    parsed.name = shape.name.empty() ? "Mesh_" + std::to_string(shapeIndex) : shape.name;
    for (PrimitiveBuildState& builder : builders) {
        auto& geometry = builder.geometry;
        if (auto valid = detail::validateTriangleMesh(geometry); !valid) {
            return std::unexpected(
                    Error::make(valid.error().code, "OBJ mesh conversion failed: " + valid.error().message));
        }

        const bool wantsTangents = options.generateMissingTangents ||
                                   materialUsesNormalTexture(scene, materialIndices, builder.sourceMaterial);
        if (wantsTangents && builder.hasCompleteTexcoords) {
            if (auto generated = detail::generateMikkTangents(geometry); !generated)
                geometry.tangents.clear();
        }

        if (!addByteCount(geometry.positions.size(), sizeof(math::FVec3), totalOutputBytes) ||
            !addByteCount(geometry.normals.size(), sizeof(math::FVec3), totalOutputBytes) ||
            !addByteCount(geometry.texcoords.size(), sizeof(math::FVec2), totalOutputBytes) ||
            !addByteCount(geometry.tangents.size(), sizeof(math::FVec4), totalOutputBytes) ||
            !addByteCount(geometry.indices.size(), sizeof(uint32_t), totalOutputBytes) ||
            totalOutputBytes > options.maxTotalAccessorBytes) {
            return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ mesh output exceeds import byte limits"));
        }

        graphics::Mesh mesh = buildStandardMesh(StandardMeshSource{
                .positions = std::span<const math::FVec3>{ geometry.positions },
                .normals = std::span<const math::FVec3>{ geometry.normals },
                .texcoords = std::span<const math::FVec2>{ geometry.texcoords },
                .tangents = std::span<const math::FVec4>{ geometry.tangents },
                .indices = std::span<const uint32_t>{ geometry.indices },
                .topology = graphics::PrimitiveTopology::TriangleList,
        });
        if (mesh.empty())
            continue;

        asset::MeshPrimitive primitive;
        primitive.mesh = std::move(mesh);
        size_t parsedMaterial = SIZE_MAX;
        if (builder.sourceMaterial >= 0 && static_cast<size_t>(builder.sourceMaterial) < materialIndices.size())
            parsedMaterial = materialIndices[static_cast<size_t>(builder.sourceMaterial)];
        if (parsedMaterial < scene.materials.size())
            primitive.name = scene.materials[parsedMaterial].name;
        parsed.primitives.push_back(std::move(primitive));
        parsed.materialIndices.push_back(parsedMaterial);
    }

    if (parsed.primitives.empty())
        return std::optional<ParsedMesh>{};
    return std::optional<ParsedMesh>{ std::move(parsed) };
}

ResultVoid importMeshes(const rapidobj::Result& source, ParsedScene& scene, const std::vector<size_t>& materialIndices,
                        const std::filesystem::path& sourcePath, const ImportOptions& options) {
    if (options.maxNodeCount == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ node count exceeds import limits"));
    }

    ParsedNode root;
    root.name = sourcePath.stem().string();
    scene.nodes.push_back(std::move(root));
    scene.rootNodes.push_back(0);

    uint64_t totalOutputBytes = 0;
    for (size_t i = 0; i < source.shapes.size(); ++i) {
        auto parsed = buildParsedMesh(source, source.shapes[i], i, materialIndices, options, scene, totalOutputBytes);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (!*parsed)
            continue;
        if (scene.nodes.size() >= options.maxNodeCount) {
            return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ node count exceeds import limits"));
        }

        const size_t meshIndex = scene.meshes.size();
        scene.meshes.push_back(std::move(**parsed));

        ParsedNode node;
        node.name = scene.meshes.back().name;
        node.parent = 0;
        node.meshIndex = meshIndex;
        scene.nodes.push_back(std::move(node));
    }

    if (scene.meshes.empty())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "OBJ contains no renderable triangle meshes"));
    return {};
}

}  // namespace

Result<ParsedScene> ObjImporter::parse(const std::string& path, const ImportOptions& options) {
    MULAN_PROFILE_ZONE();

    auto sourcePath = validateSourcePath(path, options.maxExternalFileBytes);
    if (!sourcePath)
        return std::unexpected(sourcePath.error());

    std::optional<std::filesystem::path> materialPath;
    if (options.importMaterials) {
        auto found = findMaterialLibrary(*sourcePath, options.maxExternalFileBytes);
        if (!found)
            return std::unexpected(found.error());
        materialPath = std::move(*found);
    }

    rapidobj::Result source;
    {
        MULAN_PROFILE_ZONE_N("rapidobj::ParseFile");
        if (!options.importMaterials) {
            source = rapidobj::ParseFile(*sourcePath, rapidobj::MaterialLibrary::Ignore());
        } else if (materialPath) {
            source = rapidobj::ParseFile(
                    *sourcePath, rapidobj::MaterialLibrary::SearchPath(*materialPath, rapidobj::Load::Optional));
        } else {
            source = rapidobj::ParseFile(*sourcePath, rapidobj::MaterialLibrary::Ignore());
        }
    }
    if (source.error)
        return std::unexpected(Error::make(ErrorCode::Io, rapidObjErrorMessage(source.error)));

    {
        MULAN_PROFILE_ZONE_N("rapidobj::Triangulate");
        if (!rapidobj::Triangulate(source)) {
            return std::unexpected(Error::make(ErrorCode::InvalidArg, "RapidObj failed to triangulate OBJ geometry"));
        }
    }
    if (auto valid = validateParsedData(source, options); !valid)
        return std::unexpected(valid.error());

    ParsedScene scene;
    scene.unitScale = options.unitScale > 0.0 ? options.unitScale : 1.0;
    const std::filesystem::path materialDirectory =
            materialPath ? materialPath->parent_path() : sourcePath->parent_path();
    auto materialIndices = importMaterials(source, scene, sourcePath->parent_path(), materialDirectory, options);
    if (!materialIndices)
        return std::unexpected(materialIndices.error());
    if (auto imported = importMeshes(source, scene, *materialIndices, *sourcePath, options); !imported)
        return std::unexpected(imported.error());
    return scene;
}

std::vector<std::string> ObjImporter::supportedExtensions() const {
    return { "obj" };
}

std::string ObjImporter::name() const {
    return "RapidObj Importer";
}

}  // namespace mulan::io
