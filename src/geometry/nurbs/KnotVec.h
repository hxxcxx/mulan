/**
 * @file KnotVec.h
 * @brief B样条节点向量
 *
 * 基于 truck-geometry::nurbs::KnotVec。
 * 节点向量是 B-spline/NURBS 的核心数据结构。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../Export.h"
#include <vector>
#include <optional>
#include <algorithm>
#include <stdexcept>

namespace mulan::geometry {

// 前向声明
class BasisWindow;

/// B样条节点向量
class GEOMETRY_API KnotVec {
public:
    /// 默认构造空节点向量
    KnotVec() = default;

    /// 从 double 数组构造
    explicit KnotVec(std::vector<double> knots)
        : knots_(std::move(knots)) {}

    // --- 工厂方法 ---

    /// 创建 Bezier 节点向量: [0,...,0, 1,...,1] (degree+1 个 0, degree+1 个 1)
    static KnotVec bezier_knot(size_t degree) {
        std::vector<double> kv;
        kv.reserve(2 * (degree + 1));
        for (size_t i = 0; i <= degree; ++i) kv.push_back(0.0);
        for (size_t i = 0; i <= degree; ++i) kv.push_back(1.0);
        return KnotVec(std::move(kv));
    }

    /// 创建均匀节点向量
    static KnotVec uniform_knot(size_t degree, size_t num_ctrl_pts) {
        size_t n = num_ctrl_pts + degree + 1;
        std::vector<double> kv;
        kv.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            kv.push_back(static_cast<double>(i));
        }
        return KnotVec(std::move(kv));
    }

    /// 创建 clamped 节点向量 (首尾各重复 degree+1 次)
    static KnotVec clamped_knot(size_t degree, size_t num_ctrl_pts) {
        size_t n = num_ctrl_pts + degree + 1;
        std::vector<double> kv;
        kv.reserve(n);
        size_t inner = n - 2 * (degree + 1);
        for (size_t i = 0; i <= degree; ++i) kv.push_back(0.0);
        for (size_t i = 1; i <= inner; ++i) {
            kv.push_back(static_cast<double>(i) / static_cast<double>(inner + 1));
        }
        for (size_t i = 0; i <= degree; ++i) kv.push_back(1.0);
        return KnotVec(std::move(kv));
    }

    // --- 访问 ---

    size_t len() const { return knots_.size(); }
    bool isEmpty() const { return knots_.empty(); }
    double operator[](size_t i) const { return knots_[i]; }

    const std::vector<double>& as_vec() const { return knots_; }
    std::vector<double>& as_vec_mut() { return knots_; }

    // --- 范围 ---

    /// 参数范围长度
    double rangeLength() const {
        if (isEmpty()) return 0.0;
        return knots_.back() - knots_.front();
    }

    /// 判断两个节点向量是否有相同范围
    bool sameRange(const KnotVec& other) const {
        if (isEmpty() && other.isEmpty()) return true;
        if (isEmpty() || other.isEmpty()) return false;
        return near(knots_.front(), other.knots_.front()) &&
               near(rangeLength(), other.rangeLength());
    }

    // --- 查询 ---

    /// 返回满足 self[i] <= x 的最大索引 i, 若 x < self[0] 或为空则返回 nullopt
    std::optional<size_t> floor(double x) const {
        for (size_t i = knots_.size(); i > 0; --i) {
            if (knots_[i - 1] <= x + TOLERANCE) return i - 1;
        }
        return std::nullopt;
    }

    /// 第 i 个节点的重复度
    size_t multiplicity(size_t i) const {
        size_t count = 0;
        for (size_t j = 0; j < knots_.size(); ++j) {
            if (near(knots_[j], knots_[i])) ++count;
        }
        return count;
    }

    /// 判断是否为 clamped 节点向量
    bool isClamped(size_t degree) const {
        if (knots_.size() < 2 * (degree + 1)) return false;
        for (size_t i = 0; i <= degree; ++i) {
            if (!near(knots_[i], knots_[0])) return false;
            if (!near(knots_[knots_.size() - 1 - i], knots_.back())) return false;
        }
        return true;
    }

    // --- 修改 ---

    /// 插入节点，返回插入位置
    size_t addKnot(double knot) {
        auto it = std::upper_bound(knots_.begin(), knots_.end(), knot);
        size_t idx = static_cast<size_t>(it - knots_.begin());
        knots_.insert(it, knot);
        return idx;
    }

    /// 移除第 idx 个节点 count 次
    bool removeKnot(size_t idx, size_t count) {
        if (idx >= knots_.size()) return false;
        size_t actual_count = std::min(count, knots_.size() - idx);
        knots_.erase(knots_.begin() + static_cast<ptrdiff_t>(idx),
                     knots_.begin() + static_cast<ptrdiff_t>(idx + actual_count));
        return true;
    }

    /// 归一化到 [0, 1]
    void normalize() {
        if (isEmpty()) return;
        double len = rangeLength();
        if (soSmall(len)) return;
        double offset = knots_.front();
        for (auto& k : knots_) {
            k = (k - offset) / len;
        }
    }

    /// 平移节点向量
    void translate(double x) {
        for (auto& k : knots_) {
            k += x;
        }
    }

    // --- B样条基函数 ---

    /// 计算 B-spline 基函数值
    /// @param degree 阶数
    /// @param der_rank 求导阶数 (0 = 不求导)
    /// @param t 参数值
    /// @return BasisWindow 包含基函数值和索引
    BasisWindow bsplineBasisFunctions(size_t degree, size_t der_rank, double t) const;

    /// 尝试计算 B-spline 基函数 (不 panic)
    std::optional<BasisWindow> tryBsplineBasisFunctions(size_t degree, size_t der_rank, double t) const;

private:
    std::vector<double> knots_;
};


/// B样条基函数窗口 (存储可能非零的基函数值)
class GEOMETRY_API BasisWindow {
public:
    BasisWindow() : base_(0), total_len_(0) {}

    BasisWindow(size_t base, std::vector<double> window, size_t total_len)
        : base_(base), window_(std::move(window)), total_len_(total_len) {}

    /// 基函数窗口起始索引
    size_t base() const { return base_; }

    /// 基函数值
    const std::vector<double>& as_slice() const { return window_; }

    /// 展开为完整数组 (total_len 个元素, 不在窗口内的为 0)
    std::vector<double> to_full_array() const {
        std::vector<double> full(total_len_, 0.0);
        for (size_t i = 0; i < window_.size() && base_ + i < total_len_; ++i) {
            full[base_ + i] = window_[i];
        }
        return full;
    }

    size_t size() const { return window_.size(); }
    double operator[](size_t i) const { return window_[i]; }

private:
    size_t base_;
    std::vector<double> window_;
    size_t total_len_;
};

} // namespace mulan::geometry
