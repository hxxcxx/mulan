/**
 * @file control_polygon_builder.h
 * @brief Builder for creating control polygon geometries
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "draft_geometry.h"

#include <mulan/render/camera/camera.h>

#include <span>

namespace mulan::editor {

struct ControlMarkerBasis {
    math::Vec3 x = math::Vec3::unitX();
    math::Vec3 y = math::Vec3::unitY();
    math::Vec3 normal = math::Vec3::unitZ();
};

ControlMarkerBasis controlMarkerBasisFromNormal(const math::Vec3& normal);
ControlMarkerBasis controlMarkerBasisFromCamera(const engine::Camera& camera);
double controlMarkerWorldSize(const engine::Camera& camera, const math::Point3& point, double pixels);

graphics::Mesh buildControlPointDisk(const math::Point3& center, const ControlMarkerBasis& basis, double radius);
DraftGeometry buildControlPolygonGeometry(std::span<const math::Point3> points, const ControlMarkerBasis& basis,
                                          double markerRadius);
DraftGeometry buildControlPolygonGeometry(std::span<const math::Point3> points, const ControlMarkerBasis& basis,
                                          const engine::Camera& camera, double markerPixels);

}  // namespace mulan::editor
