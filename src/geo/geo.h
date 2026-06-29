/**
 * @file geo.h
 * @brief mulan::geo 自有几何数学库 — 汇总头
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 自有几何数学库（header-only，零外部依赖）。
 * 设计要点见各子文件头注释。
 *
 * 命名空间：mulan::geo
 * 精度：Vec3 = double，FVec3 = float
 *
 * 范围说明：
 *  本模块覆盖基础解析几何（向量/矩阵/四元数/直线/平面/球/AABB/多边形）
 *  与基础求交（闭式解）。参数曲线（NURBS/Bezier/圆弧）及其数值求交
 *  见后续独立的 curve 模块，不在此处。
 */
#pragma once

// 基础
#include "GeoExport.h"
#include "GeoMath.h"
#include "Tolerance.h"
#include "Angle.h"
#include "Interval.h"

// 向量
#include "Vec2.h"
#include "Vec3.h"
#include "Vec4.h"

// 矩阵
#include "Mat2.h"
#include "Mat3.h"
#include "Mat4.h"

// 旋转
#include "Quaternion.h"

// 基础几何对象
#include "Point.h"
#include "AABB.h"
#include "Sphere.h"
#include "Line.h"
#include "Plane.h"
#include "Polygon.h"
#include "Transform.h"

// 求交
#include "Hit.h"
#include "Intersect.h"
