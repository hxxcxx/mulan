#pragma once

#include "geometry_asset.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

struct FacePlaneFrame {
    math::Point3 origin = math::Point3::origin();
    math::Vec3 x = math::Vec3::unitX();
    math::Vec3 y = math::Vec3::unitY();
    math::Vec3 normal = math::Vec3::unitZ();

    math::Plane3 plane() const { return math::Plane3::fromPointNormal(origin, normal); }
};

struct FaceLoop {
    std::vector<math::Point3> points;

    bool empty() const { return points.empty(); }
    size_t size() const { return points.size(); }
};

struct FaceDefinition {
    FacePlaneFrame frame;
    FaceLoop outer;
    std::vector<FaceLoop> holes;

    bool hasOuterLoop() const { return outer.points.size() >= 3; }
};

struct FaceRenderMeshes {
    graphics::Mesh solid;
    graphics::Mesh wire;
};

math::Point2 projectToFaceFrame(const FacePlaneFrame& frame, const math::Point3& point);
math::Point3 pointFromFaceFrame(const FacePlaneFrame& frame, const math::Point2& point);
std::vector<math::Point3> cleanFaceLoop(std::span<const math::Point3> points);
double signedFaceLoopArea(const FacePlaneFrame& frame, std::span<const math::Point3> points);
graphics::Mesh buildFaceSolidMesh(const FaceDefinition& face);
graphics::Mesh buildFaceWireMesh(const FaceDefinition& face);
FaceRenderMeshes buildFaceRenderMeshes(const FaceDefinition& face);

class FaceAsset : public GeometryAsset {
public:
    FaceAsset(AssetId id, std::string name, FaceDefinition face = {});

    const FaceDefinition& face() const { return face_; }
    void setFace(FaceDefinition face);

    const graphics::Mesh& solidMesh() const { return solid_mesh_; }
    const graphics::Mesh& wireMesh() const { return wire_mesh_; }
    bool empty() const { return face_.outer.points.empty(); }
    bool renderable() const { return !solid_mesh_.empty() || !wire_mesh_.empty(); }

    void collectDrawables(std::vector<Drawable>& out) const override;
    math::AABB3 localBounds() const override;

private:
    void rebuildRenderMeshes();

    FaceDefinition face_;
    graphics::Mesh solid_mesh_;
    graphics::Mesh wire_mesh_;
};

}  // namespace mulan::asset
