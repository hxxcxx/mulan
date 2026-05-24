/**
 * @file Face.h
 * @brief 面
 *
 * 多边界：boundaries[0] 为外环，其余为内孔。
 * 方向系统与 Edge 一致：orientation 标志位，inverse O(1)。
 *
 * 基于 truck-topology::face::Face。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "Wire.h"
#include "ID.h"
#include "Errors.h"
#include "../CurveSurface/CurveOps.h"
#include <memory>
#include <vector>
#include <functional>
#include <stdexcept>

namespace MulanGeo::brep {

/// 面
/// @tparam P 点类型
/// @tparam C 曲线几何类型
/// @tparam S 曲面几何类型
template<typename P, typename C, typename S>
class Face {
public:
    Face() = default;

    // --- 构造 ---

    /// 构造并检查有效性（边界线不相交）
    static core::Result<Face> tryNew(
        std::vector<Wire<P, C>> boundaries, S surface
    ) {
        if (boundaries.empty()) {
            return core::Err<Face>(makeError(TopologyError::EmptyWire));
        }
        if (!Wire<P, C>::disjointWires(boundaries)) {
            return core::Err<Face>(makeError(TopologyError::NotDisjointWires));
        }
        Face f;
        f.boundaries_ = std::move(boundaries);
        f.surface_ = std::make_shared<S>(std::move(surface));
        return f;
    }

    /// 构造（不检查）
    static Face newUnchecked(std::vector<Wire<P, C>> boundaries, S surface) {
        Face f;
        f.boundaries_ = std::move(boundaries);
        f.surface_ = std::make_shared<S>(std::move(surface));
        return f;
    }

    // --- 方向 ---

    bool orientation() const { return orientation_; }

    void invert() { orientation_ = !orientation_; }

    Face inverse() const {
        Face f = *this;
        f.invert();
        return f;
    }

    // --- 边界 ---

    const std::vector<Wire<P, C>>& boundaries() const { return boundaries_; }
    const Wire<P, C>& boundary(size_t i) const { return boundaries_[i]; }
    size_t numBoundaries() const { return boundaries_.size(); }

    /// 返回受 orientation 影响的边界线：
    /// orientation=true → 原样返回；orientation=false → 每条 wire 反转
    std::vector<Wire<P, C>> orientedBoundaries() const {
        std::vector<Wire<P, C>> result;
        result.reserve(boundaries_.size());
        for (const auto& w : boundaries_) {
            result.push_back(orientation_ ? w : w.inverse());
        }
        return result;
    }

    /// 外环（第一个边界）
    const Wire<P, C>& outerWire() const { return boundaries_.front(); }

    /// 孔列表（从第二个边界开始）
    std::vector<std::reference_wrapper<const Wire<P, C>>> holes() const {
        std::vector<std::reference_wrapper<const Wire<P, C>>> h;
        for (size_t i = 1; i < boundaries_.size(); ++i) {
            h.push_back(std::cref(boundaries_[i]));
        }
        return h;
    }

    /// 绝对方向边界（不受 orientation 影响）
    const std::vector<Wire<P, C>>& absoluteBoundaries() const { return boundaries_; }

    // --- 曲面 ---

    S surface() const { return *surface_; }

    /// 返回受 orientation 影响的曲面：orientation=false → 反转法线的曲面
    S orientedSurface() const {
        if (orientation_) return *surface_;
        S inv = *surface_;
        inv.invert();
        return inv;
    }

    void setSurface(S s) { *surface_ = std::move(s); }

    // --- ID ---

    FaceID<S> id() const { return FaceID<S>(surface_); }
    bool isSame(const Face& o) const { return id() == o.id(); }

    // --- 修改 ---

    /// 尝试添加边界线（检查是否与其他边界相交）
    core::Result<void> tryAddBoundary(const Wire<P, C>& wire) {
        auto all = boundaries_;
        all.push_back(wire);
        if (!Wire<P, C>::disjointWires(all)) {
            return core::Err<void>(makeError(TopologyError::NotDisjointWires));
        }
        boundaries_.push_back(wire);
        return {};
    }

    // --- 一致性检查 ---

    /// 检查曲面是否包含所有边界线
    bool isGeometricConsistent() const {
        if (!surface_) return true;
        for (const auto& wire : boundaries_) {
            for (const auto& edge : wire.edges()) {
                if (!surface_includeCurve(*surface_, edge.curve())) return false;
            }
        }
        return true;
    }

private:
    std::vector<Wire<P, C>> boundaries_;
    bool orientation_ = true;
    std::shared_ptr<S> surface_;
};

} // namespace MulanGeo::BRep
