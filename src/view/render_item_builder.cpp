#include "render_item_builder.h"

#include <algorithm>
#include <optional>

namespace mulan::view {
namespace {

constexpr uint64_t kPreviewGeometryKey = 0xF000000000000001ull;
constexpr uint64_t kPreviewMaterialKey = 0xF000000000000002ull;

bool layoutEquals(const graphics::VertexLayout& lhs, const graphics::VertexLayout& rhs) {
    if (lhs.stride() != rhs.stride() || lhs.attrCount() != rhs.attrCount() || lhs.bufferCount() != rhs.bufferCount()) {
        return false;
    }

    for (uint8_t i = 0; i < lhs.attrCount(); ++i) {
        if (!(lhs[i] == rhs[i])) {
            return false;
        }
    }
    return true;
}

bool isSurfaceLayout(const graphics::VertexLayout& layout) {
    return layoutEquals(layout, graphics::layouts::surface()) || layoutEquals(layout, graphics::layouts::pbr());
}

bool isEdgeLayout(const graphics::VertexLayout& layout) {
    return layoutEquals(layout, graphics::layouts::surface());
}

uint64_t sceneGeometryKey(asset::AssetId geometry, size_t drawableIndex) {
    return geometry.value ^ ((static_cast<uint64_t>(drawableIndex) + 1u) << 32u);
}

uint64_t previewGeometryKey(uint64_t generation, size_t drawableIndex) {
    return kPreviewGeometryKey ^ generation ^ ((static_cast<uint64_t>(drawableIndex) + 1u) << 32u);
}

std::optional<engine::RenderBucket> bucketForSceneDrawable(const asset::Drawable& drawable) {
    if (!drawable.mesh) {
        return std::nullopt;
    }

    switch (drawable.mesh->topology) {
    case graphics::PrimitiveTopology::TriangleList:
        if (drawable.role == asset::DrawableRole::Wire) {
            return std::nullopt;
        }
        return engine::RenderBucket::Surface;
    case graphics::PrimitiveTopology::LineList: return engine::RenderBucket::Edge;
    default: return std::nullopt;
    }
}

std::optional<engine::RenderBucket> bucketForPreviewMesh(const graphics::Mesh& mesh) {
    switch (mesh.topology) {
    case graphics::PrimitiveTopology::TriangleList: return engine::RenderBucket::OverlaySurface;
    case graphics::PrimitiveTopology::LineList: return engine::RenderBucket::OverlayEdge;
    default: return std::nullopt;
    }
}

bool layoutMatchesBucket(engine::RenderBucket bucket, const graphics::Mesh& mesh) {
    switch (bucket) {
    case engine::RenderBucket::Surface:
    case engine::RenderBucket::OverlaySurface: return isSurfaceLayout(mesh.layout);
    case engine::RenderBucket::Edge:
    case engine::RenderBucket::OverlayEdge: return isEdgeLayout(mesh.layout);
    case engine::RenderBucket::Gizmo:
    case engine::RenderBucket::Text: return false;
    }
    return false;
}

void countAccepted(RenderItemDiagnostics* diagnostics) {
    if (diagnostics) {
        ++diagnostics->accepted;
    }
}

void countRejectedEmpty(RenderItemDiagnostics* diagnostics) {
    if (diagnostics) {
        ++diagnostics->rejectedEmpty;
    }
}

void countRejectedTopology(RenderItemDiagnostics* diagnostics) {
    if (diagnostics) {
        ++diagnostics->rejectedTopology;
    }
}

void countRejectedLayout(RenderItemDiagnostics* diagnostics) {
    if (diagnostics) {
        ++diagnostics->rejectedLayout;
    }
}

}  // namespace

void RenderItemBuilder::buildSceneItems(asset::AssetId geometry, std::span<const asset::Drawable> drawables,
                                        std::vector<RenderItem>& out, RenderItemDiagnostics* diagnostics) {
    out.clear();
    if (diagnostics) {
        diagnostics->reset();
    }

    for (size_t i = 0; i < drawables.size(); ++i) {
        const asset::Drawable& drawable = drawables[i];
        if (!drawable.mesh || drawable.mesh->empty()) {
            countRejectedEmpty(diagnostics);
            continue;
        }

        const std::optional<engine::RenderBucket> bucket = bucketForSceneDrawable(drawable);
        if (!bucket) {
            countRejectedTopology(diagnostics);
            continue;
        }

        if (!layoutMatchesBucket(*bucket, *drawable.mesh)) {
            countRejectedLayout(diagnostics);
            continue;
        }

        out.push_back(RenderItem{
                .mesh = drawable.mesh,
                .material = drawable.material,
                .bucket = *bucket,
                .geometryKey = sceneGeometryKey(geometry, i),
                .sourceDrawableIndex = i,
        });
        countAccepted(diagnostics);
    }
}

void RenderItemBuilder::buildPreviewItems(uint64_t generation, std::span<const graphics::Mesh> meshes,
                                          std::vector<RenderItem>& out, RenderItemDiagnostics* diagnostics) {
    out.clear();
    if (diagnostics) {
        diagnostics->reset();
    }

    for (size_t i = 0; i < meshes.size(); ++i) {
        const graphics::Mesh& mesh = meshes[i];
        if (mesh.empty()) {
            countRejectedEmpty(diagnostics);
            continue;
        }

        const std::optional<engine::RenderBucket> bucket = bucketForPreviewMesh(mesh);
        if (!bucket) {
            countRejectedTopology(diagnostics);
            continue;
        }

        if (!layoutMatchesBucket(*bucket, mesh)) {
            countRejectedLayout(diagnostics);
            continue;
        }

        out.push_back(RenderItem{
                .mesh = &mesh,
                .material = asset::AssetId::invalid(),
                .bucket = *bucket,
                .geometryKey = previewGeometryKey(generation, i),
                .sourceDrawableIndex = i,
        });
        countAccepted(diagnostics);
    }
}

uint64_t RenderItemBuilder::previewMaterialKey() {
    return kPreviewMaterialKey;
}

}  // namespace mulan::view
