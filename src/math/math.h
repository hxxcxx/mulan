/**
 * @file math.h
 * @brief mulan::math 自有数学库 — 汇总头
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 自有数学库（header-only，零外部依赖）。
 * 设计要点见各子文件头注释。
 *
 * 命名空间：mulan::math
 * 精度：Vec3 = double，FVec3 = float
 *
 * 范围说明：
 *  本模块覆盖基础数值（标量/容差/角度）、线性代数（向量/矩阵/四元数/变换）、
 *  基础解析几何（直线/平面/球/AABB/多边形）与基础求交（闭式解）。
 *  参数曲线（NURBS/Bezier/圆弧）及其数值求交见 curve 子目录或独立模块。
 */
#pragma once

// 基础
#include "math_export.h"
#include "scalar/scalar.h"
#include "scalar/tolerance.h"
#include "scalar/angle.h"
#include "scalar/interval.h"

// 向量
#include "linalg/vec2.h"
#include "linalg/vec3.h"
#include "linalg/vec4.h"

// 矩阵
#include "linalg/mat2.h"
#include "linalg/mat3.h"
#include "linalg/mat4.h"

// 旋转
#include "linalg/quaternion.h"

// 基础几何对象
#include "geom/point.h"
#include "geom/aabb.h"
#include "geom/sphere.h"
#include "geom/line.h"
#include "geom/plane.h"
#include "geom/frustum.h"
#include "linalg/transform.h"

// 求交
#include "algo/hit.h"
#include "algo/intersect.h"
