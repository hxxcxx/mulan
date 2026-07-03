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
#include "geo_export.h"
#include "geo_math.h"
#include "tolerance.h"
#include "angle.h"
#include "interval.h"

// 向量
#include "vec2.h"
#include "vec3.h"
#include "vec4.h"

// 矩阵
#include "mat2.h"
#include "mat3.h"
#include "mat4.h"

// 旋转
#include "quaternion.h"

// 基础几何对象
#include "point.h"
#include "aabb.h"
#include "sphere.h"
#include "line.h"
#include "plane.h"
#include "frustum.h"
#include "polygon.h"
#include "transform.h"

// 求交
#include "hit.h"
#include "intersect.h"
