/**
 * @file frustum.h
 * @brief Engine compatibility aliases for geo frustum types.
 */
#pragma once

#include "aabb.h"

namespace mulan::engine {

using Plane = geo::Plane3;
using FrustumPlane = geo::FrustumPlane;
using Frustum = geo::Frustum3;

} // namespace mulan::engine
