/**
 * @file render_resource_delta.h
 * @brief RenderWorld 投影使用的持久 GPU 资源差量状态。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include <mulan/asset/asset_id.h>
#include <mulan/render/frontend/render_object.h>
#include <mulan/render/frontend/render_resource_prepare.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mulan::graphics {
struct Mesh;
}

namespace mulan::view::detail {

struct GeometryResourceCandidate {
    const graphics::Mesh* mesh = nullptr;
    uint64_t sourceRevision = 0;
};

using GeometryResourceCandidateMap = std::unordered_map<engine::RenderResourceKey, GeometryResourceCandidate>;

struct TextureResourceCandidate {
    std::shared_ptr<const core::Image> image;
    uint64_t contentRevision = 0;
};

using TextureResourceCandidateMap = std::unordered_map<engine::RenderTextureResourceKey, TextureResourceCandidate,
                                                       engine::RenderTextureResourceKeyHash>;

/// 从材质描述收集其当前引用的全部有效贴图身份。
void collectMaterialTextureResources(TextureResourceCandidateMap& resources,
                                     const engine::RenderMaterialDesc& material);

class RenderResourceDelta {
public:
    /// 执行域资源丢失后，下次 build 必须重新上传全部当前存活资源。
    void invalidate() { force_full_prepare_ = true; }
    bool invalidated() const { return force_full_prepare_; }

    /// 将当前存活资源与上次确认的 CPU 基线比较，生成稳定排序的 Upsert/Retire。
    void build(const GeometryResourceCandidateMap& geometries, const TextureResourceCandidateMap& textures,
               engine::RenderResourcePrepareList* prepare);

    /// 丢弃内容指纹与资源身份基线。
    void reset();

private:
    using GeometryContentRevision = std::array<uint64_t, 3>;
    using GeometryRevisionMap = std::unordered_map<engine::RenderResourceKey, GeometryContentRevision>;
    using TextureRevisionMap =
            std::unordered_map<engine::RenderTextureResourceKey, uint64_t, engine::RenderTextureResourceKeyHash>;

    GeometryRevisionMap geometry_revisions_;
    TextureRevisionMap texture_revisions_;
    bool force_full_prepare_ = true;
};

}  // namespace mulan::view::detail
