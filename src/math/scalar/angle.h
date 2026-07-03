/**
 * @file angle.h
 * @brief 强类型角度 — 统一弧度存储，区分度/弧度构造
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 设计理由：
 *  - CAD 中角度单位混淆（弧度 vs 度）是极常见 bug 源。
 *  - 用强类型 Angle 在编译期区分：必须通过 fromDeg/fromRad 构造，
 *    避免把"90"当成弧度塞进函数。
 *
 * 内部存储：始终为弧度（与 <cmath>、三角函数一致）。
 * 读取：degrees() 转度，radians() 转弧度（=原始值）。
 */
#pragma once

#include "scalar.h"

#include <cmath>

namespace mulan::math {

class Angle {
public:
    // ---------- 构造（必须显式声明单位）----------

    static constexpr Angle fromRad(double rad) { return Angle(rad); }
    static constexpr Angle fromDeg(double deg) { return Angle(deg * kDegToRad); }
    static constexpr Angle zero() { return Angle(0.0); }

    /// 半圈 / 全圈
    static constexpr Angle halfTurn() { return Angle(kPi); }
    static constexpr Angle fullTurn() { return Angle(kPi2); }

    // ---------- 读取 ----------

    constexpr double radians() const { return value_; }
    constexpr double degrees() const { return value_ * kRadToDeg; }

    // ---------- 运算 ----------

    constexpr Angle operator+(Angle o) const { return Angle(value_ + o.value_); }
    constexpr Angle operator-(Angle o) const { return Angle(value_ - o.value_); }
    constexpr Angle operator*(double s) const { return Angle(value_ * s); }
    constexpr Angle operator/(double s) const { return Angle(value_ / s); }
    friend constexpr Angle operator*(double s, Angle a) { return a * s; }

    Angle& operator+=(Angle o) { value_ += o.value_; return *this; }
    Angle& operator-=(Angle o) { value_ -= o.value_; return *this; }
    Angle  operator-() const { return Angle(-value_); }

    constexpr bool operator==(Angle o) const { return value_ == o.value_; }
    constexpr bool operator!=(Angle o) const { return value_ != o.value_; }
    constexpr bool operator<(Angle o)  const { return value_ < o.value_; }
    constexpr bool operator>(Angle o)  const { return value_ > o.value_; }
    constexpr bool operator<=(Angle o) const { return value_ <= o.value_; }
    constexpr bool operator>=(Angle o) const { return value_ >= o.value_; }

    // ---------- 三角函数（便捷）----------

    double sin() const { return std::sin(value_); }
    double cos() const { return std::cos(value_); }
    double tan() const { return std::tan(value_); }

    // ---------- 归一化 ----------

    /// 归一化到 [-π, π]
    Angle normalized() const {
        constexpr double twoPi = kPi2;
        double v = std::fmod(value_ + kPi, twoPi);
        if (v < 0.0) v += twoPi;
        return Angle(v - kPi);
    }

    /// 归一化到 [0, 2π)
    Angle normalizedPositive() const {
        double v = std::fmod(value_, kPi2);
        if (v < 0.0) v += kPi2;
        return Angle(v);
    }

    /// 取绝对值
    Angle abs() const { return Angle(value_ < 0.0 ? -value_ : value_); }

private:
    // 反三角函数构造（private：避免与单位歧义）
    static Angle asin_(double v) { return Angle(std::asin(v)); }
    static Angle acos_(double v) { return Angle(std::acos(v)); }
    static Angle atan2_(double y, double x) { return Angle(std::atan2(y, x)); }

public:
    // 反三角函数工厂（语义明确：结果为 Angle）
    static Angle fromAsin(double v) { return asin_(v); }
    static Angle fromAcos(double v) { return acos_(v); }
    static Angle fromAtan2(double y, double x) { return atan2_(y, x); }

    constexpr Angle() = default;

private:
    constexpr explicit Angle(double rad) : value_(rad) {}
    double value_ = 0.0;

    static constexpr double kDegToRad = 0.017453292519943295769; // pi/180
    static constexpr double kRadToDeg = 57.29577951308232087679; // 180/pi
};

/// 便捷别名
using Radian = Angle;
using Degree = Angle;   // 语义标签；构造仍须用 Angle::fromDeg/fromRad

} // namespace mulan::math
