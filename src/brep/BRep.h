/**
 * @file BRep.h
 * @brief BRep 模块统一入口头文件
 *
 * 当前只包含拓扑结构类。
 * 后续扩展：Curve/Surface variant、Builder、布尔运算等。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "BRepExport.h"

// 拓扑结构
#include "topology/Errors.h"
#include "topology/ID.h"
#include "topology/Vertex.h"
#include "topology/Edge.h"
#include "topology/Wire.h"
#include "topology/Face.h"
#include "topology/Shell.h"
#include "topology/Solid.h"
#include "topology/Compressed.h"

// Curve/Surface variant + visitor 分发
#include "curvesurface/CurveSurface.h"
#include "curvesurface/CurveOps.h"

// Builder + Sweep + Primitive 建模 API
#include "builder/Builder.h"
#include "builder/Sweep.h"
#include "primitive/Primitive.h"

// Tessellation + Collision
#include "algo/Triangulation.h"
#include "algo/Collision.h"

// Boolean operations
#include "algo/Boolean.h"
