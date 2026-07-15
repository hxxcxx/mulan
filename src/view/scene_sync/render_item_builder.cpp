#include "scene_sync/render_item_builder.h"

#include <mulan/render/frontend/render_contract.h>

#include <array>
#include <optional>

namespace mulan::view {
namespace {

constexpr uint64_t kPreviewGeometryNamespace = 0xF100000000000000ull;
constexpr uint64_t kPreviewToolMaterialKey = 0xF000000000000002ull;
constexpr uint64_t kPreviewSnapMaterialKey = 0xF000000000000003ull;
constexpr uint64_t kPreviewGripMaterialKey = 0xF000000000000004ull;
constexpr uint64_t kPreviewGripHotMaterialKey = 0xF000000000000005ull;

uint64_t sceneGeometryKey(asset::AssetId geometry, size_t drawableIndex) {
    return geometry.value ^ ((static_cast<uint64_t>(drawableIndex) + 1u) << 32u);
}

constexpr size_t kPreviewRoleCount = static_cast<size_t>(PreviewVisualRole::GripHot) + 1u;

size_t previewRoleIndex(PreviewVisualRole role) {
    switch (role) {
    case PreviewVisualRole::Tool: return 0;
    case PreviewVisualRole::Snap: return 1;
    case PreviewVisualRole::Grip: return 2;
    case PreviewVisualRole::GripHot: return 3;
    }
    return 0;
}

uint64_t previewGeometryKey(PreviewVisualRole role, size_t roleSlot) {
    constexpr uint64_t kRoleShift = 48u;
    constexpr uint64_t kSlotMask = (1ull << kRoleShift) - 1ull;
    const uint64_t roleBits = (static_cast<uint64_t>(previewRoleIndex(role)) + 1ull) << kRoleShift;
    const uint64_t slotBits = (static_cast<uint64_t>(roleSlot) + 1ull) & kSlotMask;
    return kPreviewGeometryNamespace | roleBits | slotBits;
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

void RenderItemBuilder::buildPreviewItems(std::span<const PreviewDrawable> drawables, std::vector<RenderItem>& out,
                                          RenderItemDiagnostics* diagnostics) {
    out.clear();
    if (diagnostics) {
        diagnostics->reset();
    }

    std::array<size_t, kPreviewRoleCount> roleSlots{};
    for (size_t i = 0; i < drawables.size(); ++i) {
        const PreviewDrawable& drawable = drawables[i];
        const size_t roleIndex = previewRoleIndex(drawable.role);
        const size_t roleSlot = roleSlots[roleIndex]++;
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
                .geometryKey = previewGeometryKey(drawable.role, roleSlot),
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
