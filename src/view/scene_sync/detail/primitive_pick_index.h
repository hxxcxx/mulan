/**
 * @file primitive_pick_index.h
 * @brief 为几何资产的三角形与线段提供版本化、紧凑的静态拾取 BVH。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 索引只保存本地空间 AABB 与稳定的 drawable/primitive 编号，不持有 Mesh
 * 指针。资产内容 revision 改变时整项重建；实体变换和相机变化不会重建。
 * 构建遇到非法顶点或非有限 bounds 时明确拒绝，调用方必须退回旧线性精确路径。
 */

#pragma once

#include <mulan/asset/geometry_asset.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace mulan::view::detail {

enum class PickPrimitiveKind : uint8_t {
    Triangle,
    Segment,
};

struct PickPrimitiveRef {
    uint32_t drawableIndex = 0;
    uint32_t primitiveIndex = 0;
    PickPrimitiveKind kind = PickPrimitiveKind::Triangle;

    friend constexpr bool operator==(PickPrimitiveRef, PickPrimitiveRef) = default;
};

struct PrimitivePickQueryStats {
    size_t indexedPrimitiveCount = 0;
    size_t nodeBoundsTestCount = 0;
    size_t leafPrimitiveCount = 0;
    size_t candidatePrimitiveCount = 0;
    bool usedIndex = false;
    bool linearFallback = false;
};

class PrimitivePickIndex {
public:
    /// 构建完整索引；false 表示必须由调用方线性 fallback，禁止使用残缺树。
    bool build(std::span<const asset::Drawable> drawables);

    /// 查询可能命中的图元，结果恢复为旧路径的 drawable/primitive 稳定顺序。
    void queryRay(const math::Ray3& localRay, double localPadding, std::vector<PickPrimitiveRef>& out,
                  PrimitivePickQueryStats* stats = nullptr) const;

    size_t primitiveCount() const { return primitives_.size(); }
    size_t nodeCount() const { return nodes_.size(); }

private:
    struct Primitive {
        PickPrimitiveRef ref;
        math::AABB3 bounds;
        math::Point3 centroid;
    };

    struct Node {
        math::AABB3 bounds;
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
        uint32_t right = 0;

        bool leaf() const { return count != 0; }
    };

    uint32_t buildNode(uint32_t first, uint32_t count);
    void appendAll(std::vector<PickPrimitiveRef>& out) const;

    std::vector<Primitive> primitives_;
    std::vector<Node> nodes_;
};

/// RenderScene 私有的惰性缓存。domain 切换会先清空，避免不同文档同值 AssetId 碰撞。
class PrimitivePickIndexCache {
public:
    void bindDomain(uint64_t domain);
    void clear();
    void erase(asset::AssetId asset);

    /// 返回当前 revision 的完整索引；nullptr 表示该 revision 必须线性 fallback。
    const PrimitivePickIndex* get(const asset::GeometryAsset& asset, std::span<const asset::Drawable> drawables) const;

    size_t entryCount() const { return entries_.size(); }
    size_t buildCount() const { return build_count_; }

private:
    struct Entry {
        asset::AssetRevision revision = 0;
        bool indexable = false;
        PrimitivePickIndex index;
    };

    uint64_t domain_ = 0;
    mutable size_t build_count_ = 0;
    mutable std::unordered_map<asset::AssetId, Entry> entries_;
};

}  // namespace mulan::view::detail
