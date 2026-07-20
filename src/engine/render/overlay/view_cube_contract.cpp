#include "view_cube_contract.h"

namespace mulan::engine {

ViewCubeRect ViewCubeLayout::rect(uint32_t viewportWidth, uint32_t viewportHeight) const {
    const int32_t s = static_cast<int32_t>(size);
    const int32_t m = static_cast<int32_t>(margin);
    const int32_t w = static_cast<int32_t>(viewportWidth);
    const int32_t h = static_cast<int32_t>(viewportHeight);
    ViewCubeRect result{ .width = s, .height = s };
    switch (corner) {
    case ViewCubeCorner::TopLeft:
        result.x = m;
        result.y = m;
        break;
    case ViewCubeCorner::TopRight:
        result.x = w - s - m;
        result.y = m;
        break;
    case ViewCubeCorner::BottomLeft:
        result.x = m;
        result.y = h - s - m;
        break;
    case ViewCubeCorner::BottomRight:
        result.x = w - s - m;
        result.y = h - s - m;
        break;
    }
    return result;
}

const std::array<ViewCubePart, ViewCubeGeometry::kPartCount>& ViewCubeGeometry::parts() {
    static constexpr std::array<ViewCubePart, kPartCount> parts = {
        ViewCubePart{ ViewCubePartType::Face, 0, 0, 1, 0 },
        ViewCubePart{ ViewCubePartType::Face, 0, 0, -1, 1 },
        ViewCubePart{ ViewCubePartType::Face, -1, 0, 0, 2 },
        ViewCubePart{ ViewCubePartType::Face, 1, 0, 0, 3 },
        ViewCubePart{ ViewCubePartType::Face, 0, 1, 0, 4 },
        ViewCubePart{ ViewCubePartType::Face, 0, -1, 0, 5 },
        ViewCubePart{ ViewCubePartType::Edge, -1, -1, 0, 6 },
        ViewCubePart{ ViewCubePartType::Edge, -1, 1, 0, 7 },
        ViewCubePart{ ViewCubePartType::Edge, 1, -1, 0, 8 },
        ViewCubePart{ ViewCubePartType::Edge, 1, 1, 0, 9 },
        ViewCubePart{ ViewCubePartType::Edge, -1, 0, -1, 10 },
        ViewCubePart{ ViewCubePartType::Edge, -1, 0, 1, 11 },
        ViewCubePart{ ViewCubePartType::Edge, 1, 0, -1, 12 },
        ViewCubePart{ ViewCubePartType::Edge, 1, 0, 1, 13 },
        ViewCubePart{ ViewCubePartType::Edge, 0, -1, -1, 14 },
        ViewCubePart{ ViewCubePartType::Edge, 0, -1, 1, 15 },
        ViewCubePart{ ViewCubePartType::Edge, 0, 1, -1, 16 },
        ViewCubePart{ ViewCubePartType::Edge, 0, 1, 1, 17 },
        ViewCubePart{ ViewCubePartType::Corner, -1, -1, -1, 18 },
        ViewCubePart{ ViewCubePartType::Corner, -1, -1, 1, 19 },
        ViewCubePart{ ViewCubePartType::Corner, -1, 1, -1, 20 },
        ViewCubePart{ ViewCubePartType::Corner, -1, 1, 1, 21 },
        ViewCubePart{ ViewCubePartType::Corner, 1, -1, -1, 22 },
        ViewCubePart{ ViewCubePartType::Corner, 1, -1, 1, 23 },
        ViewCubePart{ ViewCubePartType::Corner, 1, 1, -1, 24 },
        ViewCubePart{ ViewCubePartType::Corner, 1, 1, 1, 25 },
    };
    return parts;
}

math::Vec3 ViewCubeGeometry::partNormal(const ViewCubePart& part) {
    return math::Vec3(static_cast<double>(part.x), static_cast<double>(part.y), static_cast<double>(part.z))
            .normalized();
}

ViewCubeFace ViewCubeGeometry::faceForPart(const ViewCubePart& part) {
    if (part.z > 0)
        return ViewCubeFace::Top;
    if (part.z < 0)
        return ViewCubeFace::Bottom;
    if (part.x < 0)
        return ViewCubeFace::Left;
    if (part.x > 0)
        return ViewCubeFace::Right;
    if (part.y > 0)
        return ViewCubeFace::Back;
    return ViewCubeFace::Front;
}

ViewCubePartShape ViewCubeGeometry::partShape(const ViewCubePart& part) {
    ViewCubePartShape shape;
    shape.part = part;
    const double h = ViewCubeStyle::CubeHalfExtent;
    const double c = ViewCubeStyle::CenterHalfExtent;
    const auto point = [](double x, double y, double z) {
        return math::Vec3(x, y, z);
    };
    if (part.type == ViewCubePartType::Face) {
        shape.vertexCount = 4;
        if (part.x != 0) {
            const double x = part.x * h;
            shape.vertices = { point(x, -c, -c), point(x, c, -c), point(x, c, c), point(x, -c, c) };
        } else if (part.y != 0) {
            const double y = part.y * h;
            shape.vertices = { point(-c, y, -c), point(c, y, -c), point(c, y, c), point(-c, y, c) };
        } else {
            const double z = part.z * h;
            shape.vertices = { point(-c, -c, z), point(c, -c, z), point(c, c, z), point(-c, c, z) };
        }
    } else if (part.type == ViewCubePartType::Edge) {
        shape.vertexCount = 4;
        if (part.z == 0)
            shape.vertices = { point(part.x * h, part.y * c, -c), point(part.x * h, part.y * c, c),
                               point(part.x * c, part.y * h, c), point(part.x * c, part.y * h, -c) };
        else if (part.y == 0)
            shape.vertices = { point(part.x * h, -c, part.z * c), point(part.x * h, c, part.z * c),
                               point(part.x * c, c, part.z * h), point(part.x * c, -c, part.z * h) };
        else
            shape.vertices = { point(-c, part.y * h, part.z * c), point(c, part.y * h, part.z * c),
                               point(c, part.y * c, part.z * h), point(-c, part.y * c, part.z * h) };
    } else if (part.type == ViewCubePartType::Corner) {
        shape.vertexCount = 3;
        shape.vertices[0] = point(part.x * h, part.y * c, part.z * c);
        shape.vertices[1] = point(part.x * c, part.y * h, part.z * c);
        shape.vertices[2] = point(part.x * c, part.y * c, part.z * h);
    }
    return shape;
}

}  // namespace mulan::engine
