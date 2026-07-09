#include "transform_preview_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/graphics/vertex/vertex_semantic.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/scene.h>

#include <cstring>
#include <utility>

namespace mulan::app {
namespace {

const asset::GeometryAsset* geometryAssetFor(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets || !scene->isValid(entity)) {
        return nullptr;
    }

    const scene::GeometryComponent* geometry = scene->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<const asset::GeometryAsset*>(assets->asset(geometry->geometry));
}

bool readFloatVector(const std::vector<std::byte>& bytes, size_t offset, graphics::VertexFormat format,
                     math::Vec3& value, float* fourth = nullptr) {
    if (format == graphics::VertexFormat::Float3) {
        float raw[3]{};
        std::memcpy(raw, bytes.data() + offset, sizeof(raw));
        value = math::Vec3(raw[0], raw[1], raw[2]);
        return true;
    }
    if (format == graphics::VertexFormat::Float4) {
        float raw[4]{};
        std::memcpy(raw, bytes.data() + offset, sizeof(raw));
        value = math::Vec3(raw[0], raw[1], raw[2]);
        if (fourth) {
            *fourth = raw[3];
        }
        return true;
    }
    return false;
}

bool writeFloatVector(std::vector<std::byte>& bytes, size_t offset, graphics::VertexFormat format,
                      const math::Vec3& value, float fourth = 1.0F) {
    if (format == graphics::VertexFormat::Float3) {
        const float raw[3]{ static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
        std::memcpy(bytes.data() + offset, raw, sizeof(raw));
        return true;
    }
    if (format == graphics::VertexFormat::Float4) {
        const float raw[4]{ static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z),
                            fourth };
        std::memcpy(bytes.data() + offset, raw, sizeof(raw));
        return true;
    }
    return false;
}

bool attributeRangeValid(const graphics::Mesh& mesh, const graphics::VertexAttribute& attribute, uint32_t vertex) {
    const size_t offset = static_cast<size_t>(vertex) * mesh.vertexStride() + attribute.offset;
    return offset + attribute.size() <= mesh.vertices.size();
}

bool transformPositions(graphics::Mesh& mesh, const math::Mat4& worldTransform) {
    const graphics::VertexAttribute* position = mesh.layout.find(graphics::VertexSemantic::Position);
    if (!position || mesh.vertexStride() == 0) {
        return false;
    }
    if (position->format != graphics::VertexFormat::Float3 && position->format != graphics::VertexFormat::Float4) {
        return false;
    }

    const uint32_t vertexCount = mesh.vertexCount();
    for (uint32_t i = 0; i < vertexCount; ++i) {
        if (!attributeRangeValid(mesh, *position, i)) {
            return false;
        }

        const size_t offset = static_cast<size_t>(i) * mesh.vertexStride() + position->offset;
        math::Vec3 value;
        float fourth = 1.0F;
        if (!readFloatVector(mesh.vertices, offset, position->format, value, &fourth)) {
            return false;
        }

        const math::Point3 transformed = math::Point3(value).transformedBy(worldTransform);
        if (!writeFloatVector(mesh.vertices, offset, position->format, transformed.asVec(), fourth)) {
            return false;
        }
    }

    mesh.computeBounds();
    return true;
}

void transformVectorAttribute(graphics::Mesh& mesh, graphics::VertexSemantic semantic, const math::Mat4& worldTransform,
                              bool asNormal) {
    const graphics::VertexAttribute* attribute = mesh.layout.find(semantic);
    if (!attribute || mesh.vertexStride() == 0) {
        return;
    }
    if (attribute->format != graphics::VertexFormat::Float3 && attribute->format != graphics::VertexFormat::Float4) {
        return;
    }

    const uint32_t vertexCount = mesh.vertexCount();
    for (uint32_t i = 0; i < vertexCount; ++i) {
        if (!attributeRangeValid(mesh, *attribute, i)) {
            return;
        }

        const size_t offset = static_cast<size_t>(i) * mesh.vertexStride() + attribute->offset;
        math::Vec3 value;
        float fourth = 1.0F;
        if (!readFloatVector(mesh.vertices, offset, attribute->format, value, &fourth)) {
            return;
        }

        const math::Vec3 transformed =
                asNormal ? value.transformedAsNormal(worldTransform) : value.transformedAsDir(worldTransform);
        writeFloatVector(mesh.vertices, offset, attribute->format, transformed, fourth);
    }
}

bool transformMeshInPlace(graphics::Mesh& mesh, const math::Mat4& worldTransform) {
    if (!transformPositions(mesh, worldTransform)) {
        return false;
    }

    transformVectorAttribute(mesh, graphics::VertexSemantic::Normal, worldTransform, true);
    transformVectorAttribute(mesh, graphics::VertexSemantic::Tangent, worldTransform, false);
    return true;
}

}  // namespace

DraftGeometry TransformPreviewBuilder::build(const io::Document& document,
                                             std::span<const EntityTransformUpdate> updates) {
    std::vector<graphics::Mesh> meshes;
    for (const EntityTransformUpdate& update : updates) {
        if (!update.valid()) {
            continue;
        }

        const asset::GeometryAsset* geometry = geometryAssetFor(document, update.entity);
        if (!geometry) {
            continue;
        }

        std::vector<asset::Drawable> drawables;
        geometry->collectDrawables(drawables);
        for (const asset::Drawable& drawable : drawables) {
            if (!drawable.mesh || drawable.mesh->empty()) {
                continue;
            }

            graphics::Mesh mesh = *drawable.mesh;
            if (!transformMeshInPlace(mesh, update.worldTransform)) {
                continue;
            }
            meshes.push_back(std::move(mesh));
        }
    }

    return DraftGeometry::meshes(std::move(meshes));
}

}  // namespace mulan::app
