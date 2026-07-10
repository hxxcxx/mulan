#include "import_builder.h"

#include <mulan/graphics/vertex/vertex_buffer.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace mulan::io {
namespace {

template <typename T>
void appendValue(std::vector<std::byte>& bytes, const T& value) {
    const auto oldSize = bytes.size();
    bytes.resize(oldSize + sizeof(T));
    std::memcpy(bytes.data() + oldSize, &value, sizeof(T));
}

}  // namespace

graphics::Mesh buildStandardMesh(const StandardMeshSource& source) {
    if (source.positions.empty())
        return {};

    graphics::Mesh mesh;
    const bool useTangents = source.tangents.size() >= source.positions.size();
    mesh.layout = useTangents ? graphics::layouts::pbr() : graphics::layouts::surface();
    mesh.topology = source.topology;

    graphics::VertexBufferBuilder vertices(mesh.layout, static_cast<uint32_t>(source.positions.size()));
    for (uint32_t i = 0; i < source.positions.size(); ++i) {
        const math::FVec3& position = source.positions[i];
        vertices.setPosition(i, position.x, position.y, position.z);

        const math::FVec3 normal = i < source.normals.size() ? source.normals[i] : math::FVec3(0.0f, 0.0f, 1.0f);
        vertices.setNormal(i, normal.x, normal.y, normal.z);

        const math::FVec2 uv = i < source.texcoords.size() ? source.texcoords[i] : math::FVec2(0.0f);
        float uvData[2] = { uv.x, uv.y };
        vertices.write(i, graphics::VertexSemantic::TexCoord0, uvData);

        if (useTangents) {
            const math::FVec4& tangent = source.tangents[i];
            float tangentData[4] = { tangent.x, tangent.y, tangent.z, tangent.w };
            vertices.write(i, graphics::VertexSemantic::Tangent, tangentData);
        }
    }

    auto vertexBytes = vertices.data();
    mesh.vertices.assign(vertexBytes.begin(), vertexBytes.end());

    const size_t indexCount = source.indices.empty() ? source.positions.size() : source.indices.size();
    const bool hasLargeIndex =
            std::any_of(source.indices.begin(), source.indices.end(), [](uint32_t index) { return index > 0xFFFFu; });
    const bool use32Bit = source.force32BitIndices || source.positions.size() > 0xFFFFu || hasLargeIndex;
    mesh.indexType = use32Bit ? graphics::IndexType::UInt32 : graphics::IndexType::UInt16;
    mesh.indices.reserve(indexCount * graphics::indexTypeSize(mesh.indexType));

    for (size_t i = 0; i < indexCount; ++i) {
        const uint32_t index = source.indices.empty() ? static_cast<uint32_t>(i) : source.indices[i];
        if (use32Bit) {
            appendValue(mesh.indices, index);
        } else {
            appendValue(mesh.indices, static_cast<uint16_t>(index));
        }
    }

    mesh.computeBounds();
    return mesh;
}

}  // namespace mulan::io
