#include "view_cube_model.h"

#include <cmath>

namespace mulan::view {

engine::ViewCubeHit ViewCubeModel::hitTest(int32_t screenX, int32_t screenY, uint32_t viewportWidth,
                                           uint32_t viewportHeight) const {
    const engine::ViewCubeRect rect = layout_.rect(viewportWidth, viewportHeight);
    if (!rect.contains(screenX, screenY))
        return {};
    return engine::ViewCubeHit{
        .type = engine::ViewCubePartType::Face,
        .localX = screenX - rect.x,
        .localY = screenY - rect.y,
    };
}

engine::ViewCubeHit ViewCubeModel::pickPart(int32_t screenX, int32_t screenY, uint32_t viewportWidth,
                                            uint32_t viewportHeight, const math::Mat4& mainViewMatrix) const {
    engine::ViewCubeHit hit = hitTest(screenX, screenY, viewportWidth, viewportHeight);
    if (!hit)
        return hit;

    const engine::ViewCubeRect rect = layout_.rect(viewportWidth, viewportHeight);
    const double ndcX = 2.0 * static_cast<double>(hit.localX) / static_cast<double>(rect.width) - 1.0;
    const double ndcY = 1.0 - 2.0 * static_cast<double>(hit.localY) / static_cast<double>(rect.height);
    math::Mat4 cubeView{ math::Mat3(mainViewMatrix) };
    cubeView[3] = math::Vec4(0, 0, -engine::ViewCubeStyle::ViewDistance, 1);
    const math::Mat4 cubeProjection =
            math::Mat4::ortho(-engine::ViewCubeStyle::OrthoExtent, engine::ViewCubeStyle::OrthoExtent,
                              -engine::ViewCubeStyle::OrthoExtent, engine::ViewCubeStyle::OrthoExtent, 0.1, 10.0);
    const math::Mat4 inverseViewProjection = (cubeProjection * cubeView).inverse();
    math::Vec4 nearPoint = inverseViewProjection * math::Vec4(ndcX, ndcY, -1.0, 1.0);
    math::Vec4 farPoint = inverseViewProjection * math::Vec4(ndcX, ndcY, 1.0, 1.0);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const math::Vec3 rayOrigin(nearPoint.x, nearPoint.y, nearPoint.z);
    const math::Vec3 rayDirection = (math::Vec3(farPoint) - rayOrigin).normalized();

    const auto intersects = [&](const math::Vec3& a, const math::Vec3& b, const math::Vec3& c, double& distance) {
        const math::Vec3 edge1 = b - a;
        const math::Vec3 edge2 = c - a;
        const math::Vec3 cross = rayDirection.cross(edge2);
        const double determinant = edge1.dot(cross);
        if (std::abs(determinant) < 1.0e-10)
            return false;
        const double inverse = 1.0 / determinant;
        const math::Vec3 offset = rayOrigin - a;
        const double u = offset.dot(cross) * inverse;
        if (u < 0.0 || u > 1.0)
            return false;
        const math::Vec3 q = offset.cross(edge1);
        const double v = rayDirection.dot(q) * inverse;
        if (v < 0.0 || u + v > 1.0)
            return false;
        distance = edge2.dot(q) * inverse;
        return distance >= 0.0;
    };

    double bestDistance = 1.0e100;
    engine::ViewCubePart bestPart;
    for (const engine::ViewCubePart& part : engine::ViewCubeGeometry::parts()) {
        const engine::ViewCubePartShape shape = engine::ViewCubeGeometry::partShape(part);
        double distance = 0.0;
        bool matched = intersects(shape.vertices[0], shape.vertices[1], shape.vertices[2], distance);
        if (!matched && shape.vertexCount == 4)
            matched = intersects(shape.vertices[0], shape.vertices[2], shape.vertices[3], distance);
        if (matched && distance < bestDistance) {
            bestDistance = distance;
            bestPart = part;
        }
    }
    if (bestDistance == 1.0e100) {
        hit.type = engine::ViewCubePartType::None;
        return hit;
    }
    hit.type = bestPart.type;
    hit.face = engine::ViewCubeGeometry::faceForPart(bestPart);
    hit.part = bestPart;
    return hit;
}

}  // namespace mulan::view
