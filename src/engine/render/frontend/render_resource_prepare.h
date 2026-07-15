/**
 * @file render_resource_prepare.h
 * @brief 描述可靠渲染资源阶段的几何/贴图新增、更新与退役操作。
 * @author hxxcxx
 * @date 2026-07-06
 */

#pragma once

#include "../asset_gpu_key.h"

#include <mulan/core/image/image.h>
#include <mulan/graphics/mesh.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::engine {

/// 同一完整资源身份的持久操作。
enum class RenderResourceOperation : uint8_t {
    Upsert,
    Retire,
};

struct RenderGeometryPrepareDesc {
    RenderResourceKey resourceKey;
    /// 提交层拥有的不可变网格快照。渲染端可以跨线程读取，
    /// 不依赖 GeometryAsset 或 PreviewLayer 的生命周期。
    std::shared_ptr<const graphics::Mesh> mesh;
    bool forceUpdate = false;
    RenderResourceOperation operation = RenderResourceOperation::Upsert;

    bool isUpsert() const { return operation == RenderResourceOperation::Upsert; }
    bool isRetire() const { return operation == RenderResourceOperation::Retire; }
};

/// 持久贴图的完整 GPU 身份。同一资产在线性/sRGB 或 mip 意图不同时
/// 会产生不同实例，退役时不得只使用资源来源字段误删另一实例。
struct RenderTextureResourceKey {
    RenderResourceKey resourceKey;
    bool srgb = false;
    bool generateMips = true;

    constexpr explicit operator bool() const noexcept { return static_cast<bool>(resourceKey); }
    constexpr bool operator==(const RenderTextureResourceKey&) const = default;
};

struct RenderTextureResourceKeyHash {
    size_t operator()(const RenderTextureResourceKey& key) const noexcept {
        size_t value = std::hash<RenderResourceKey>{}(key.resourceKey);
        value ^= static_cast<size_t>(key.srgb) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        value ^= static_cast<size_t>(key.generateMips) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        return value;
    }
};

struct RenderTexturePrepareDesc {
    RenderTextureResourceKey identity;
    /// 可靠批次自持有的不可变像素快照，不依赖 TextureAsset 生命周期。
    std::shared_ptr<const core::Image> image;
    uint64_t contentRevision = 0;
    RenderResourceOperation operation = RenderResourceOperation::Upsert;

    bool isUpsert() const { return operation == RenderResourceOperation::Upsert; }
    bool isRetire() const { return operation == RenderResourceOperation::Retire; }
};

class RenderResourcePrepareList {
public:
    void clear() {
        geometries_.clear();
        geometry_indices_.clear();
        textures_.clear();
        texture_indices_.clear();
    }

    bool empty() const { return geometries_.empty() && textures_.empty(); }
    size_t size() const { return geometries_.size() + textures_.size(); }

    void addGeometry(RenderResourceKey resourceKey, const graphics::Mesh& mesh, bool forceUpdate = false) {
        if (!resourceKey || mesh.empty()) {
            return;
        }
        addGeometry(RenderGeometryPrepareDesc{
                .resourceKey = resourceKey,
                .mesh = std::make_shared<graphics::Mesh>(mesh),
                .forceUpdate = forceUpdate,
                .operation = RenderResourceOperation::Upsert,
        });
    }

    /// 将持久几何从执行域退役；不存在的键按幂等空操作处理。
    void retireGeometry(RenderResourceKey resourceKey) {
        if (!resourceKey) {
            return;
        }
        addGeometry(RenderGeometryPrepareDesc{
                .resourceKey = resourceKey,
                .operation = RenderResourceOperation::Retire,
        });
    }

    /// 同键采用最后操作获胜：upsert→retire 仅保留退役，retire→upsert 仅保留新快照。
    /// 因此未确认批次可安全吸收后续场景更新，不会对同一 key 执行过期操作。
    void addGeometry(RenderGeometryPrepareDesc desc) {
        if (!desc.resourceKey) {
            return;
        }
        if (desc.isUpsert()) {
            if (!desc.mesh || desc.mesh->empty()) {
                return;
            }
        } else if (desc.isRetire()) {
            desc.mesh.reset();
            desc.forceUpdate = false;
        } else {
            return;
        }
        if (const auto found = geometry_indices_.find(desc.resourceKey); found != geometry_indices_.end()) {
            RenderGeometryPrepareDesc& current = geometries_[found->second];
            // retire 可能尚未到达执行端；后续 upsert 必须能覆盖旧实例，
            // 不能因 registry 仍命中而误当作已是最新资源。
            if (current.isRetire() && desc.isUpsert()) {
                desc.forceUpdate = true;
            }
            current = std::move(desc);
            return;
        }

        const size_t index = geometries_.size();
        auto [inserted, added] = geometry_indices_.emplace(desc.resourceKey, index);
        if (!added) {
            return;
        }
        try {
            geometries_.push_back(std::move(desc));
        } catch (...) {
            geometry_indices_.erase(inserted);
            throw;
        }
    }

    void addTexture(RenderTextureResourceKey identity, std::shared_ptr<const core::Image> image,
                    uint64_t contentRevision) {
        addTexture(RenderTexturePrepareDesc{
                .identity = identity,
                .image = std::move(image),
                .contentRevision = contentRevision,
                .operation = RenderResourceOperation::Upsert,
        });
    }

    /// 将指定资产键及加载意图对应的贴图实例退役。
    void retireTexture(RenderTextureResourceKey identity) {
        if (!identity) {
            return;
        }
        addTexture(RenderTexturePrepareDesc{
                .identity = identity,
                .operation = RenderResourceOperation::Retire,
        });
    }

    /// 同一贴图完整身份也采用最后操作获胜。因此未确认的 retire
    /// 能被后续重现的 upsert 覆盖，不会在新帧使用前执行过期退役。
    void addTexture(RenderTexturePrepareDesc desc) {
        if (!desc.identity) {
            return;
        }
        if (desc.isUpsert()) {
            if (!desc.image || !desc.image->valid()) {
                return;
            }
        } else if (desc.isRetire()) {
            desc.image.reset();
            desc.contentRevision = 0;
        } else {
            return;
        }

        if (const auto found = texture_indices_.find(desc.identity); found != texture_indices_.end()) {
            textures_[found->second] = std::move(desc);
            return;
        }

        const size_t index = textures_.size();
        auto [inserted, added] = texture_indices_.emplace(desc.identity, index);
        if (!added) {
            return;
        }
        try {
            textures_.push_back(std::move(desc));
        } catch (...) {
            texture_indices_.erase(inserted);
            throw;
        }
    }

    void merge(const RenderResourcePrepareList& other) {
        for (const auto& geometry : other.geometries_) {
            addGeometry(geometry);
        }
        for (const auto& texture : other.textures_) {
            addTexture(texture);
        }
    }

    std::span<const RenderGeometryPrepareDesc> geometries() const { return geometries_; }
    std::span<const RenderTexturePrepareDesc> textures() const { return textures_; }

private:
    std::vector<RenderGeometryPrepareDesc> geometries_;
    std::unordered_map<RenderResourceKey, size_t> geometry_indices_;
    std::vector<RenderTexturePrepareDesc> textures_;
    std::unordered_map<RenderTextureResourceKey, size_t, RenderTextureResourceKeyHash> texture_indices_;
};

}  // namespace mulan::engine
