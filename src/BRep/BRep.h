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
#include "Topology/Errors.h"
#include "Topology/ID.h"
#include "Topology/Vertex.h"
#include "Topology/Edge.h"
#include "Topology/Wire.h"
#include "Topology/Face.h"
#include "Topology/Shell.h"
#include "Topology/Solid.h"
#include "Topology/Compressed.h"

// Curve/Surface variant + visitor 分发
#include "CurveSurface/CurveSurface.h"
#include "CurveSurface/CurveOps.h"

// Builder + Sweep + Primitive 建模 API
#include "Builder/Builder.h"
#include "Builder/Sweep.h"
#include "Primitive/Primitive.h"
