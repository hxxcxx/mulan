/**
 * @file primitive_pick_index.cpp
 * @brief 资产图元静态拾取 BVH 的构建、查询与版本缓存实现。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "primitive_pick_index.h"

#include "mesh_picking.h"

#include <mulan/math/algo/intersect.h>
#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mulan::view::detail {
namespace {

constexpr uint32_t MaxLeafPrimitives = 4;

bool finitePoint(const math::Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool validBounds(const math::AABB3& bounds) {
    return finitePoint(bounds.min) && finitePoint(bounds.max) && bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

math::Point3 boundsCentroid(const math::AABB3& bounds) {
    return bounds.min + (bounds.max - bounds.min) * 0.5;
}

bool refLess(const PickPrimitiveRef& lhs, const PickPrimitiveRef& rhs) {
    if (lhs.drawableIndex != rhs.drawableIndex)
        return lhs.drawableIndex < rhs.drawableIndex;
    if (lhs.primitiveIndex != rhs.primitiveIndex)
        return lhs.primitiveIndex < rhs.primitiveIndex;
    return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
}

bool finiteRay(const math::Ray3& ray) {
    const double lengthSq = ray.direction.lengthSq();
    return finitePoint(ray.origin) && std::isfinite(ray.direction.x) && std::isfinite(ray.direction.y) &&
           std::isfinite(ray.direction.z) && std::isfinite(lengthSq) && lengthSq > 0.0;
}

math::AABB3 expanded(const math::AABB3& bounds, double padding) {
    if (!(padding > 0.0))
        return bounds;
    const math::Vec3 amount(padding, padding, padding);
    return math::AABB3(bounds.min - amount, bounds.max + amount);
}

}  // namespace

bool PrimitivePickIndex::build(std::span<const asset::Drawable> drawables) {
    MULAN_PROFILE_ZONE();

    primitives_.clear();
    nodes_.clear();

    for (size_t drawableIndex = 0; drawableIndex < drawables.size(); ++drawableIndex) {
        if (drawableIndex > std::numeric_limits<uint32_t>::max())
            return false;
        const graphics::Mesh* mesh = drawables[drawableIndex].mesh;
        if (!mesh || mesh->empty() || !mesh->layout.has(graphics::VertexSemantic::Position))
            continue;

        uint32_t primitiveCount = 0;
        PickPrimitiveKind kind = PickPrimitiveKind::Triangle;
        switch (mesh->topology) {
        case graphics::PrimitiveTopology::TriangleList:
            primitiveCount = !mesh->indices.empty() ? mesh->indexCount() / 3u : mesh->vertexCount() / 3u;
            kind = PickPrimitiveKind::Triangle;
            break;
        case graphics::PrimitiveTopology::LineList:
        case graphics::PrimitiveTopology::LineStrip:
            primitiveCount = lineSegmentCount(*mesh);
            kind = PickPrimitiveKind::Segment;
            break;
        default: continue;
        }

        for (uint32_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
            // Node/Primitive 均使用 uint32_t 索引。宁可拒绝索引并回退线性路径，
            // 也不能让极端资产发生截断后构造一棵不完整的树。
            if (primitives_.size() >= std::numeric_limits<uint32_t>::max())
                return false;
            math::AABB3 bounds;
            if (kind == PickPrimitiveKind::Triangle) {
                math::Point3 v0;
                math::Point3 v1;
                math::Point3 v2;
                if (!readTriangle(*mesh, primitiveIndex, v0, v1, v2))
                    return false;
                bounds.expand(v0);
                bounds.expand(v1);
                bounds.expand(v2);
            } else {
                math::Point3 v0;
                math::Point3 v1;
                if (!readLineSegment(*mesh, primitiveIndex, v0, v1))
                    return false;
                bounds.expand(v0);
                bounds.expand(v1);
            }
            if (!validBounds(bounds))
                return false;
            primitives_.push_back(Primitive{
                    .ref = { static_cast<uint32_t>(drawableIndex), primitiveIndex, kind },
                    .bounds = bounds,
                    .centroid = boundsCentroid(bounds),
            });
        }
    }

    if (!primitives_.empty()) {
        const size_t leafCapacity =
                (primitives_.size() + static_cast<size_t>(MaxLeafPrimitives) - 1u) / MaxLeafPrimitives;
        if (leafCapacity > (std::numeric_limits<uint32_t>::max() + size_t{ 1 }) / 2u)
            return false;
        // 满二叉树节点数为 2*leaf-1；按图元 2*N-1 预留会在百万图元资产上
        // 无谓放大约四倍 Node 内存。
        nodes_.reserve(leafCapacity * 2u - 1u);
        buildNode(0, static_cast<uint32_t>(primitives_.size()));
    }
    return true;
}

uint32_t PrimitivePickIndex::buildNode(uint32_t first, uint32_t count) {
    const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({});

    math::AABB3 bounds;
    math::AABB3 centroidBounds;
    for (uint32_t index = first; index < first + count; ++index) {
        bounds.expand(primitives_[index].bounds);
        centroidBounds.expand(primitives_[index].centroid);
    }
    nodes_[nodeIndex].bounds = bounds;
    if (count <= MaxLeafPrimitives) {
        nodes_[nodeIndex].first = first;
        nodes_[nodeIndex].count = count;
        return nodeIndex;
    }

    const math::Vec3 centroidSize = centroidBounds.max - centroidBounds.min;
    int axis = 0;
    if (centroidSize.y > centroidSize.x)
        axis = 1;
    if (centroidSize.z > centroidSize[axis])
        axis = 2;
    const uint32_t middle = first + count / 2u;
    std::nth_element(primitives_.begin() + first, primitives_.begin() + middle, primitives_.begin() + first + count,
                     [axis](const Primitive& lhs, const Primitive& rhs) {
                         if (lhs.centroid[axis] != rhs.centroid[axis])
                             return lhs.centroid[axis] < rhs.centroid[axis];
                         return refLess(lhs.ref, rhs.ref);
                     });

    const uint32_t left = buildNode(first, middle - first);
    const uint32_t right = buildNode(middle, first + count - middle);
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = right;
    return nodeIndex;
}

void PrimitivePickIndex::appendAll(std::vector<PickPrimitiveRef>& out) const {
    out.reserve(out.size() + primitives_.size());
    for (const Primitive& primitive : primitives_)
        out.push_back(primitive.ref);
    std::sort(out.begin(), out.end(), refLess);
}

void PrimitivePickIndex::queryRay(const math::Ray3& localRay, double localPadding, std::vector<PickPrimitiveRef>& out,
                                  PrimitivePickQueryStats* stats) const {
    out.clear();
    PrimitivePickQueryStats localStats;
    localStats.indexedPrimitiveCount = primitives_.size();
    if (primitives_.empty()) {
        if (stats)
            *stats = localStats;
        return;
    }

    if (!finiteRay(localRay) || !std::isfinite(localPadding)) {
        appendAll(out);
        localStats.leafPrimitiveCount = primitives_.size();
        localStats.candidatePrimitiveCount = out.size();
        if (stats)
            *stats = localStats;
        return;
    }
    const double padding = std::max(0.0, localPadding);
    std::vector<uint32_t> stack;
    stack.reserve(64);
    stack.push_back(0);
    while (!stack.empty()) {
        const uint32_t nodeIndex = stack.back();
        stack.pop_back();
        const Node& node = nodes_[nodeIndex];
        ++localStats.nodeBoundsTestCount;
        if (!math::intersect(localRay, expanded(node.bounds, padding)).hit)
            continue;
        if (node.leaf()) {
            localStats.leafPrimitiveCount += node.count;
            for (uint32_t index = node.first; index < node.first + node.count; ++index)
                out.push_back(primitives_[index].ref);
            continue;
        }
        stack.push_back(node.right);
        stack.push_back(node.left);
    }

    std::sort(out.begin(), out.end(), refLess);
    localStats.candidatePrimitiveCount = out.size();
    if (stats)
        *stats = localStats;
}

void PrimitivePickIndexCache::bindDomain(uint64_t domain) {
    if (domain_ == domain)
        return;
    domain_ = domain;
    clear();
}

void PrimitivePickIndexCache::clear() {
    entries_.clear();
}

void PrimitivePickIndexCache::erase(asset::AssetId asset) {
    entries_.erase(asset);
}

const PrimitivePickIndex* PrimitivePickIndexCache::get(const asset::GeometryAsset& asset,
                                                       std::span<const asset::Drawable> drawables) const {
    Entry& entry = entries_[asset.id()];
    if (entry.revision != asset.revision()) {
        entry = {};
        entry.revision = asset.revision();
        entry.indexable = entry.index.build(drawables);
        if (!entry.indexable) {
            // 非法顶点可能出现在大资产尾部；fallback 项不能继续持有此前已收集的
            // 半成品图元，否则一次恶意/损坏资产即可长期占用整份 BVH 内存。
            entry.index = {};
        }
        ++build_count_;
    }
    return entry.indexable ? &entry.index : nullptr;
}

}  // namespace mulan::view::detail
