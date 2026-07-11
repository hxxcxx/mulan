#include "render_item_builder.h"

#include <mulan/render/frontend/render_contract.h>

#include <optional>

namespace mulan::view {
namespace {

constexpr uint64_t kPreviewGeometryKey = 0xF000000000000001ull;
constexpr uint64_t kPreviewToolMaterialKey = 0xF000000000000002ull;
constexpr uint64_t kPreviewSnapMaterialKey = 0xF000000000000003ull;
constexpr uint64_t kPreviewGripMaterialKey = 0xF000000000000004ull;
constexpr uint64_t kPreviewGripHotMaterialKey = 0xF000000000000005ull;

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

        if (!engine::renderBucketAcceptsTopology(*bucket, drawable.mesh->topology)) {
            countRejectedTopology(diagnostics);
            continue;
        }

        if (!engine::renderBucketAcceptsLayout(*bucket, drawable.mesh->layout)) {
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

void RenderItemBuilder::buildPreviewItems(uint64_t generation, std::span<const PreviewDrawable> drawables,
                                          std::vector<RenderItem>& out, RenderItemDiagnostics* diagnostics) {
    out.clear();
    if (diagnostics) {
        diagnostics->reset();
    }

    for (size_t i = 0; i < drawables.size(); ++i) {
        const PreviewDrawable& drawable = drawables[i];
        const graphics::Mesh& mesh = drawable.mesh;
        if (mesh.empty()) {
            countRejectedEmpty(diagnostics);
            continue;
        }

        const std::optional<engine::RenderBucket> bucket = bucketForPreviewMesh(mesh);
        if (!bucket) {
            countRejectedTopology(diagnostics);
            continue;
        }

        if (!engine::renderBucketAcceptsTopology(*bucket, mesh.topology)) {
            countRejectedTopology(diagnostics);
            continue;
        }

        if (!engine::renderBucketAcceptsLayout(*bucket, mesh.layout)) {
            countRejectedLayout(diagnostics);
            continue;
        }

        out.push_back(RenderItem{
                .mesh = &mesh,
                .material = asset::AssetId::invalid(),
                .bucket = *bucket,
                .geometryKey = previewGeometryKey(generation, i),
                .sourceDrawableIndex = i,
                .previewRole = drawable.role,
        });
        countAccepted(diagnostics);
    }
}

uint64_t RenderItemBuilder::previewMaterialKey(PreviewVisualRole role) {
    switch (role) {
    case PreviewVisualRole::Tool: return kPreviewToolMaterialKey;
    case PreviewVisualRole::Snap: return kPreviewSnapMaterialKey;
    case PreviewVisualRole::Grip: return kPreviewGripMaterialKey;
    case PreviewVisualRole::GripHot: return kPreviewGripHotMaterialKey;
    }
    return kPreviewToolMaterialKey;
}

}  // namespace mulan::view
