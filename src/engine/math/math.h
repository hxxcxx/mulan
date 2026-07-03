/**
 * @file math.h
 * @brief Engine math facade backed by mulan::geo.
 *
 * Keep engine::Vec3/Mat4/etc. as the stable public names while the underlying
 * implementation moves from GLM to the project-owned geo module.
 */
#pragma once

#include <mulan/geo/geo.h>

namespace mulan::engine {

using Vec2 = geo::Vec2;
using Vec3 = geo::Vec3;
using Vec4 = geo::Vec4;
using Mat2 = geo::Mat2;
using Mat3 = geo::Mat3;
using Mat4 = geo::Mat4;
using Quat = geo::Quat;

using FVec2 = geo::FVec2;
using FVec3 = geo::FVec3;
using FVec4 = geo::FVec4;
using FMat2 = geo::FMat2;
using FMat3 = geo::FMat3;
using FMat4 = geo::FMat4;
using FQuat = geo::FQuat;

using geo::clamp;
using geo::degrees;
using geo::kHalfPi;
using geo::kPi;
using geo::kPi2;
using geo::max;
using geo::min;
using geo::ortho;
using geo::perspective;
using geo::radians;
using geo::scale;
using geo::translate;

} // namespace mulan::engine
