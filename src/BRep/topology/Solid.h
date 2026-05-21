/**
 * @file Solid.h
 * @brief 实体（壳的集合）
 *
 * 多个边界壳构成实体。支持反转和拓扑编辑操作。
 *
 * 基于 truck-topology::solid::Solid。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "Shell.h"
#include "Errors.h"
#include <vector>
#include <tl/expected.hpp>
#include <optional>
#include <stdexcept>

namespace MulanGeo::BRep {

/// 实体
template<typename P, typename C, typename S>
class Solid {
public:
    Solid() = default;

    // --- 构造 ---

    static tl::expected<Solid, TopologyError> tryNew(
        std::vector<Shell<P, C, S>> boundaries
    ) {
        if (boundaries.empty()) {
            return tl::unexpected(TopologyError::EmptyShell);
        }
        // 每个边界壳必须是闭合的
        for (const auto& shell : boundaries) {
            if (!shell.isClosed()) {
                return tl::unexpected(TopologyError::NotClosedShell);
            }
        }
        return Solid(std::move(boundaries));
    }

    static Solid newUnchecked(std::vector<Shell<P, C, S>> boundaries) {
        return Solid(std::move(boundaries));
    }

    // --- 访问 ---

    const std::vector<Shell<P, C, S>>& boundaries() const { return boundaries_; }
    size_t numBoundaries() const { return boundaries_.size(); }
    const Shell<P, C, S>& boundary(size_t i) const { return boundaries_[i]; }

    // --- 方向 ---

    void invert() {
        for (auto& shell : boundaries_) {
            shell.invert();
        }
    }

    /// 取反（返回内部/外部反转的实体）
    Solid operator!() const {
        Solid s = *this;
        s.invert();
        return s;
    }

    // --- 拓扑编辑（布尔运算的低级原语） ---

    /// 在指定边上插入一个顶点，将边一分为二
    std::optional<std::pair<Edge<P, C>, Edge<P, C>>>
    cutEdge(EdgeID<C> id, const Vertex<P>& v) {
        (void)id;
        (void)v;
        // 暂未实现，后续布尔运算时补充
        return std::nullopt;
    }

    /// 移除一个顶点，合并相邻两条边
    std::optional<Edge<P, C>>
    removeVertexByConcatEdges(VertexID<P> id) {
        (void)id;
        // 暂未实现
        return std::nullopt;
    }

    /// 在面上插入一条边，将一个面一分为二
    bool cutFaceByEdge(FaceID<S> id, const Edge<P, C>& e) {
        (void)id;
        (void)e;
        // 暂未实现
        return false;
    }

    // --- 一致性检查 ---

    bool isGeometricConsistent() const {
        // 检查每个壳的几何一致性
        for (const auto& shell : boundaries_) {
            if (!shell.shellCondition() == ShellCondition::Closed &&
                !shell.shellCondition() == ShellCondition::Oriented) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<Shell<P, C, S>> boundaries_;

    explicit Solid(std::vector<Shell<P, C, S>> boundaries)
        : boundaries_(std::move(boundaries)) {}
};

} // namespace MulanGeo::BRep
