#include "mesh_attribute_generator.h"

#include <mikktspace.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace mulan::io::detail {
namespace {

constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
constexpr float kMinimumVectorLengthSquared = 1.0e-20f;
constexpr float kEquivalentTangentDot = 1.0f - 1.0e-5f;
constexpr std::string_view kInvalidVertexStreams = "invalid or non-finite vertex attribute streams";
constexpr std::string_view kInvalidTriangleIndices = "invalid triangle-list indices";
constexpr std::string_view kMeshTooLarge = "mesh exceeds supported 32-bit attribute generation limits";
constexpr std::string_view kTangentGenerationFailed = "MikkTSpace tangent generation failed";
constexpr std::string_view kInvalidGeneratedTangents = "MikkTSpace produced an invalid tangent basis";

bool isFinite(const math::FVec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool isFinite(const math::FVec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isFinite(const math::FVec4& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

template <typename T>
bool isFiniteStream(const std::vector<T>& values) noexcept {
    return std::all_of(values.begin(), values.end(), [](const T& value) { return isFinite(value); });
}

size_t cornerCount(const TriangleMeshData& mesh) noexcept {
    return mesh.indices.empty() ? mesh.positions.size() : mesh.indices.size();
}

uint32_t sourceIndexAt(const TriangleMeshData& mesh, size_t corner) noexcept {
    return mesh.indices.empty() ? static_cast<uint32_t>(corner) : mesh.indices[corner];
}

math::FVec3 fallbackTangent(const math::FVec3& normal) noexcept {
    const math::FVec3 axis = std::abs(normal.z) < 0.9f ? math::FVec3::unitZ() : math::FVec3::unitY();
    return axis.cross(normal).normalizedOr(math::FVec3::unitX());
}

struct MikkUserData {
    const TriangleMeshData* mesh = nullptr;
    std::vector<math::FVec4> cornerTangents;
};

const MikkUserData& userData(const SMikkTSpaceContext* context) noexcept {
    return *static_cast<const MikkUserData*>(context->m_pUserData);
}

MikkUserData& mutableUserData(const SMikkTSpaceContext* context) noexcept {
    return *static_cast<MikkUserData*>(context->m_pUserData);
}

size_t cornerOffset(int face, int vertex) noexcept {
    return static_cast<size_t>(face) * 3u + static_cast<size_t>(vertex);
}

int getNumFaces(const SMikkTSpaceContext* context) noexcept {
    return static_cast<int>(userData(context).cornerTangents.size() / 3u);
}

int getNumVerticesOfFace(const SMikkTSpaceContext*, int) noexcept {
    return 3;
}

void getPosition(const SMikkTSpaceContext* context, float output[], int face, int vertex) noexcept {
    const auto& data = userData(context);
    const auto& value = data.mesh->positions[sourceIndexAt(*data.mesh, cornerOffset(face, vertex))];
    output[0] = value.x;
    output[1] = value.y;
    output[2] = value.z;
}

void getNormal(const SMikkTSpaceContext* context, float output[], int face, int vertex) noexcept {
    const auto& data = userData(context);
    const auto value = data.mesh->normals[sourceIndexAt(*data.mesh, cornerOffset(face, vertex))].normalizedOr(
            math::FVec3::unitZ());
    output[0] = value.x;
    output[1] = value.y;
    output[2] = value.z;
}

void getTexCoord(const SMikkTSpaceContext* context, float output[], int face, int vertex) noexcept {
    const auto& data = userData(context);
    const auto& value = data.mesh->texcoords[sourceIndexAt(*data.mesh, cornerOffset(face, vertex))];
    output[0] = value.x;
    output[1] = value.y;
}

void setTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[], float sign, int face,
                    int vertex) noexcept {
    mutableUserData(context).cornerTangents[cornerOffset(face, vertex)] =
            math::FVec4(tangent[0], tangent[1], tangent[2], sign);
}

bool equivalentTangent(const math::FVec4& lhs, const math::FVec4& rhs) noexcept {
    if (lhs.w != rhs.w)
        return false;
    return lhs.xyz().dot(rhs.xyz()) >= kEquivalentTangentDot;
}

}  // namespace

ResultVoid validateTriangleMesh(const TriangleMeshData& mesh) {
    const size_t corners = cornerCount(mesh);
    if (mesh.positions.empty() || corners < 3u || corners % 3u != 0u)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidTriangleIndices));
    if (mesh.positions.size() > std::numeric_limits<uint32_t>::max() || corners > std::numeric_limits<uint32_t>::max())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kMeshTooLarge));

    if (!isFiniteStream(mesh.positions))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidVertexStreams));
    if ((!mesh.normals.empty() && (mesh.normals.size() != mesh.positions.size() || !isFiniteStream(mesh.normals))) ||
        (!mesh.texcoords.empty() &&
         (mesh.texcoords.size() != mesh.positions.size() || !isFiniteStream(mesh.texcoords))) ||
        (!mesh.tangents.empty() && (mesh.tangents.size() != mesh.positions.size() || !isFiniteStream(mesh.tangents))))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidVertexStreams));

    if (!mesh.indices.empty() && std::any_of(mesh.indices.begin(), mesh.indices.end(),
                                             [&](uint32_t index) { return index >= mesh.positions.size(); }))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidTriangleIndices));

    return {};
}

ResultVoid generateFlatNormals(TriangleMeshData& mesh) {
    if (auto valid = validateTriangleMesh(mesh); !valid)
        return valid;

    const size_t corners = cornerCount(mesh);
    TriangleMeshData output;
    output.positions.reserve(corners);
    output.normals.reserve(corners);
    output.indices.reserve(corners);
    if (!mesh.texcoords.empty())
        output.texcoords.reserve(corners);

    for (size_t corner = 0; corner < corners; corner += 3u) {
        const uint32_t i0 = sourceIndexAt(mesh, corner);
        const uint32_t i1 = sourceIndexAt(mesh, corner + 1u);
        const uint32_t i2 = sourceIndexAt(mesh, corner + 2u);
        const math::FVec3 faceNormal = (mesh.positions[i1] - mesh.positions[i0])
                                               .cross(mesh.positions[i2] - mesh.positions[i0])
                                               .normalizedOr(math::FVec3::unitZ());

        for (const uint32_t sourceIndex : { i0, i1, i2 }) {
            output.positions.push_back(mesh.positions[sourceIndex]);
            output.normals.push_back(faceNormal);
            if (!mesh.texcoords.empty())
                output.texcoords.push_back(mesh.texcoords[sourceIndex]);
            output.indices.push_back(static_cast<uint32_t>(output.indices.size()));
        }
    }

    mesh = std::move(output);
    return {};
}

ResultVoid generateMikkTangents(TriangleMeshData& mesh) {
    if (auto valid = validateTriangleMesh(mesh); !valid)
        return valid;
    if (mesh.normals.size() != mesh.positions.size() || mesh.texcoords.size() != mesh.positions.size())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidVertexStreams));

    const size_t corners = cornerCount(mesh);
    const size_t faceCount = corners / 3u;
    if (faceCount > static_cast<size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, kMeshTooLarge));

    for (const auto& normal : mesh.normals) {
        if (normal.lengthSq() <= kMinimumVectorLengthSquared)
            return std::unexpected(Error::make(ErrorCode::InvalidArg, kInvalidVertexStreams));
    }

    MikkUserData data;
    data.mesh = &mesh;
    data.cornerTangents.resize(corners);

    SMikkTSpaceInterface interface = {
        .m_getNumFaces = getNumFaces,
        .m_getNumVerticesOfFace = getNumVerticesOfFace,
        .m_getPosition = getPosition,
        .m_getNormal = getNormal,
        .m_getTexCoord = getTexCoord,
        .m_setTSpaceBasic = setTSpaceBasic,
        .m_setTSpace = nullptr,
    };
    const SMikkTSpaceContext context = {
        .m_pInterface = &interface,
        .m_pUserData = &data,
    };
    if (!genTangSpaceDefault(&context))
        return std::unexpected(Error::make(ErrorCode::Internal, kTangentGenerationFailed));

    for (size_t corner = 0; corner < corners; ++corner) {
        auto& tangent = data.cornerTangents[corner];
        if (!isFinite(tangent))
            return std::unexpected(Error::make(ErrorCode::Internal, kInvalidGeneratedTangents));

        const auto normal = mesh.normals[sourceIndexAt(mesh, corner)].normalizedOr(math::FVec3::unitZ());
        auto direction = tangent.xyz() - normal * normal.dot(tangent.xyz());
        direction = direction.normalizedOr(fallbackTangent(normal));
        tangent = math::FVec4(direction, tangent.w < 0.0f ? -1.0f : 1.0f);
    }

    // 同一三角形内的手性必须一致，否则插值后的切线基没有定义。
    for (size_t corner = 0; corner < corners; corner += 3u) {
        const float sign = data.cornerTangents[corner].w;
        if (data.cornerTangents[corner + 1u].w != sign || data.cornerTangents[corner + 2u].w != sign)
            return std::unexpected(Error::make(ErrorCode::Internal, kInvalidGeneratedTangents));
    }

    TriangleMeshData output;
    output.positions.reserve(mesh.positions.size());
    output.normals.reserve(mesh.normals.size());
    output.texcoords.reserve(mesh.texcoords.size());
    output.tangents.reserve(mesh.positions.size());
    output.indices.reserve(corners);

    // 每个源顶点维护一个轻量链表，避免 vector<vector<...>> 在大模型上的额外分配。
    std::vector<uint32_t> firstVariant(mesh.positions.size(), kInvalidIndex);
    std::vector<uint32_t> nextVariant;
    nextVariant.reserve(mesh.positions.size());

    for (size_t corner = 0; corner < corners; ++corner) {
        const uint32_t sourceIndex = sourceIndexAt(mesh, corner);
        const math::FVec4 tangent = data.cornerTangents[corner];

        uint32_t outputIndex = firstVariant[sourceIndex];
        while (outputIndex != kInvalidIndex && !equivalentTangent(output.tangents[outputIndex], tangent))
            outputIndex = nextVariant[outputIndex];

        if (outputIndex == kInvalidIndex) {
            if (output.positions.size() >= std::numeric_limits<uint32_t>::max())
                return std::unexpected(Error::make(ErrorCode::InvalidArg, kMeshTooLarge));
            outputIndex = static_cast<uint32_t>(output.positions.size());
            output.positions.push_back(mesh.positions[sourceIndex]);
            output.normals.push_back(mesh.normals[sourceIndex].normalizedOr(math::FVec3::unitZ()));
            output.texcoords.push_back(mesh.texcoords[sourceIndex]);
            output.tangents.push_back(tangent);
            nextVariant.push_back(firstVariant[sourceIndex]);
            firstVariant[sourceIndex] = outputIndex;
        }
        output.indices.push_back(outputIndex);
    }

    mesh = std::move(output);
    return {};
}

}  // namespace mulan::io::detail
