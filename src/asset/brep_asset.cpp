#include "brep_asset.h"

namespace mulan::asset {

BRepAsset::BRepAsset(AssetId id, std::string name, modeling::Shape shape)
    : GeometryAsset(id, AssetKind::BRep, std::move(name)), shape_(std::move(shape)) {
}

void BRepAsset::setShape(modeling::Shape shape) {
    shape_ = std::move(shape);
    geometry_dirty_ = true;
}

bool BRepAsset::ensureTessellated() const {
    if (!geometry_dirty_)
        return !geometry_.solidMesh.empty() || !geometry_.wireMesh.empty();

    geometry_ = modeling::TessellatedGeometry{};
    if (shape_.empty()) {
        geometry_dirty_ = false;
        return false;
    }

    auto result = shape_.tessellate(tess_options_);
    geometry_dirty_ = false;
    if (!result)
        return false;

    geometry_ = std::move(*result);
    return !geometry_.solidMesh.empty() || !geometry_.wireMesh.empty();
}

void BRepAsset::collectDrawables(std::vector<Drawable>& out) const {
    if (!ensureTessellated())
        return;

    if (!geometry_.solidMesh.empty())
        out.push_back({ &geometry_.solidMesh, AssetId::invalid(), DrawableRole::Solid });
    if (!geometry_.wireMesh.empty())
        out.push_back({ &geometry_.wireMesh, AssetId::invalid(), DrawableRole::Wire });
}

math::AABB3 BRepAsset::localBounds() const {
    if (!ensureTessellated())
        return shape_.bounds();

    math::AABB3 bounds = math::AABB3::empty();
    if (!geometry_.solidMesh.bounds.isEmpty())
        bounds.expand(geometry_.solidMesh.bounds);
    if (!geometry_.wireMesh.bounds.isEmpty())
        bounds.expand(geometry_.wireMesh.bounds);
    return bounds;
}

}  // namespace mulan::asset
