/**
 * @file Geometry.h
 * @brief 几何库统一入口
 *
 * 包含所有几何类型定义。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

// 导出宏
#include "Export.h"

// 基础类型
#include "Types.h"
#include "Tolerance.h"
#include "BoundingBox.h"
#include "Newton.h"

// 特征接口
#include "traits/ParametricCurve.h"
#include "traits/ParametricSurface.h"
#include "traits/Transformable.h"
#include "traits/SearchParameter.h"

// NURBS 核心
#include "nurbs/KnotVec.h"
#include "nurbs/BSplineCurve.h"
#include "nurbs/BSplineSurface.h"
#include "nurbs/NurbsCurve.h"
#include "nurbs/NurbsSurface.h"

// 解析曲线
#include "specified/Line.h"
#include "specified/Circle.h"
#include "specified/Ellipse.h"
#include "specified/Hyperbola.h"
#include "specified/Parabola.h"

// 解析曲面
#include "specified/Plane.h"
#include "specified/Sphere.h"
#include "specified/Torus.h"
#include "specified/CylinderSurface.h"
#include "specified/ConeSurface.h"

// 装饰器
#include "decorators/Processor.h"
#include "decorators/TrimmedCurve.h"
#include "decorators/ExtrudedCurve.h"
#include "decorators/RevolutedCurve.h"
#include "decorators/PCurve.h"
#include "decorators/OffsetCurve.h"
#include "decorators/BlendCurve.h"

// 算法
#include "algo/curve/presearch.h"
#include "algo/surface/search.h"
