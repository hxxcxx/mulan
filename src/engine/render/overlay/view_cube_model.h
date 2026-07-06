#pragma once

#include <mulan/math/math.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace mulan::engine {

enum class ViewCubeCorner : uint8_t {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class ViewCubePartType : uint8_t {
    None,
    Face,
    Edge,
    Corner,
};

enum class ViewCubeFace : uint8_t {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
};

struct ViewCubePart {
    ViewCubePartType type = ViewCubePartType::None;
    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;
    uint32_t index = 0;

    friend constexpr bool operator==(const ViewCubePart& a, const ViewCubePart& b) {
        return a.type == b.type && a.x == b.x && a.y == b.y && a.z == b.z && a.index == b.index;
    }
};

struct ViewCubePartShape {
    ViewCubePart part;
    std::array<math::Vec3, 4> vertices{};
    uint32_t vertexCount = 0;
};

struct ViewCubeRect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    bool contains(int32_t px, int32_t py) const { return px >= x && py >= y && px < x + width && py < y + height; }
};

struct ViewCubeStyle {
    static constexpr uint32_t ViewportSize = 208;
    static constexpr double CubeHalfExtent = 0.58;
    static constexpr double CenterHalfExtent = 0.44;
    static constexpr double OrthoExtent = 1.36;
    static constexpr double ViewDistance = 3.5;
    static constexpr double LabelSurfaceOffset = 0.004;
    static constexpr float LabelSizePx = 48.0f;
    static constexpr float LabelSizeWorld = 0.145f;
    static constexpr float LabelFacingFadeStart = -0.02f;
    static constexpr float LabelFacingFadeEnd = 0.18f;
    static constexpr float AxisOrigin = -0.60f;
    static constexpr float AxisEnd = 0.62f;
    static constexpr float AxisConeLength = 0.14f;
    static constexpr float AxisShaftRadius = 0.020f;
    static constexpr float AxisConeRadius = 0.058f;
};

struct ViewCubeLayout {
    uint32_t size = ViewCubeStyle::ViewportSize;
    uint32_t margin = 16;
    ViewCubeCorner corner = ViewCubeCorner::BottomRight;

    ViewCubeRect rect(uint32_t viewportWidth, uint32_t viewportHeight) const {
        const int32_t s = static_cast<int32_t>(size);
        const int32_t m = static_cast<int32_t>(margin);
        const int32_t w = static_cast<int32_t>(viewportWidth);
        const int32_t h = static_cast<int32_t>(viewportHeight);

        ViewCubeRect r;
        r.width = s;
        r.height = s;

        switch (corner) {
        case ViewCubeCorner::TopLeft:
            r.x = m;
            r.y = m;
            break;
        case ViewCubeCorner::TopRight:
            r.x = w - s - m;
            r.y = m;
            break;
        case ViewCubeCorner::BottomLeft:
            r.x = m;
            r.y = h - s - m;
            break;
        case ViewCubeCorner::BottomRight:
            r.x = w - s - m;
            r.y = h - s - m;
            break;
        }
        return r;
    }
};

struct ViewCubeHit {
    ViewCubePartType type = ViewCubePartType::None;
    ViewCubeFace face = ViewCubeFace::Front;
    ViewCubePart part;
    int32_t localX = 0;
    int32_t localY = 0;

    explicit operator bool() const { return type != ViewCubePartType::None; }
};

struct ViewCubeInteractionState {
    ViewCubePart hoveredPart;
    ViewCubePart pressedPart;
    bool hasHoveredPart = false;
    bool hasPressedPart = false;
};

class ViewCubeModel {
public:
    static constexpr uint32_t kPartCount = 26;

    explicit ViewCubeModel(ViewCubeLayout layout = {}) : layout_(layout) {}

    const ViewCubeLayout& layout() const { return layout_; }
    void setLayout(const ViewCubeLayout& layout) { layout_ = layout; }

    ViewCubeRect viewportRect(uint32_t viewportWidth, uint32_t viewportHeight) const {
        return layout_.rect(viewportWidth, viewportHeight);
    }

    static const std::array<ViewCubePart, kPartCount>& parts() {
        static constexpr std::array<ViewCubePart, kPartCount> kParts = {
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
        return kParts;
    }

    static math::Vec3 partNormal(const ViewCubePart& part) {
        return math::Vec3(static_cast<double>(part.x), static_cast<double>(part.y), static_cast<double>(part.z))
                .normalized();
    }

    static ViewCubeFace faceForPart(const ViewCubePart& part) {
        if (part.z > 0)
            return ViewCubeFace::Front;
        if (part.z < 0)
            return ViewCubeFace::Back;
        if (part.x < 0)
            return ViewCubeFace::Left;
        if (part.x > 0)
            return ViewCubeFace::Right;
        if (part.y > 0)
            return ViewCubeFace::Top;
        return ViewCubeFace::Bottom;
    }

    static ViewCubePartShape partShape(const ViewCubePart& part) {
        ViewCubePartShape shape;
        shape.part = part;

        const double h = ViewCubeStyle::CubeHalfExtent;
        const double c = ViewCubeStyle::CenterHalfExtent;
        auto makePoint = [](double x, double y, double z) {
            return math::Vec3(x, y, z);
        };

        if (part.type == ViewCubePartType::Face) {
            shape.vertexCount = 4;
            if (part.x != 0) {
                const double x = part.x * h;
                shape.vertices = { makePoint(x, -c, -c), makePoint(x, c, -c), makePoint(x, c, c), makePoint(x, -c, c) };
            } else if (part.y != 0) {
                const double y = part.y * h;
                shape.vertices = { makePoint(-c, y, -c), makePoint(c, y, -c), makePoint(c, y, c), makePoint(-c, y, c) };
            } else {
                const double z = part.z * h;
                shape.vertices = { makePoint(-c, -c, z), makePoint(c, -c, z), makePoint(c, c, z), makePoint(-c, c, z) };
            }
            return shape;
        }

        if (part.type == ViewCubePartType::Edge) {
            shape.vertexCount = 4;
            if (part.z == 0) {
                shape.vertices = { makePoint(part.x * h, part.y * c, -c), makePoint(part.x * h, part.y * c, c),
                                   makePoint(part.x * c, part.y * h, c), makePoint(part.x * c, part.y * h, -c) };
            } else if (part.y == 0) {
                shape.vertices = { makePoint(part.x * h, -c, part.z * c), makePoint(part.x * h, c, part.z * c),
                                   makePoint(part.x * c, c, part.z * h), makePoint(part.x * c, -c, part.z * h) };
            } else {
                shape.vertices = { makePoint(-c, part.y * h, part.z * c), makePoint(c, part.y * h, part.z * c),
                                   makePoint(c, part.y * c, part.z * h), makePoint(-c, part.y * c, part.z * h) };
            }
            return shape;
        }

        if (part.type == ViewCubePartType::Corner) {
            shape.vertexCount = 3;
            shape.vertices[0] = makePoint(part.x * h, part.y * c, part.z * c);
            shape.vertices[1] = makePoint(part.x * c, part.y * h, part.z * c);
            shape.vertices[2] = makePoint(part.x * c, part.y * c, part.z * h);
        }
        return shape;
    }

    ViewCubeHit hitTest(int32_t screenX, int32_t screenY, uint32_t viewportWidth, uint32_t viewportHeight) const {
        const ViewCubeRect r = viewportRect(viewportWidth, viewportHeight);
        if (!r.contains(screenX, screenY)) {
            return {};
        }

        ViewCubeHit hit;
        hit.type = ViewCubePartType::Face;
        hit.localX = screenX - r.x;
        hit.localY = screenY - r.y;
        return hit;
    }

    ViewCubeHit pickPart(int32_t screenX, int32_t screenY, uint32_t viewportWidth, uint32_t viewportHeight,
                         const math::Mat4& mainViewMatrix) const {
        ViewCubeHit hit = hitTest(screenX, screenY, viewportWidth, viewportHeight);
        if (!hit) {
            return hit;
        }

        const ViewCubeRect r = viewportRect(viewportWidth, viewportHeight);
        const double ndcX = (2.0 * static_cast<double>(hit.localX)) / static_cast<double>(r.width) - 1.0;
        const double ndcY = 1.0 - (2.0 * static_cast<double>(hit.localY)) / static_cast<double>(r.height);

        math::Mat3 rotOnly(mainViewMatrix);
        math::Mat4 cubeView(rotOnly);
        cubeView[3] = math::Vec4(0, 0, -ViewCubeStyle::ViewDistance, 1);
        const math::Mat4 cubeProj =
                math::Mat4::ortho(-ViewCubeStyle::OrthoExtent, ViewCubeStyle::OrthoExtent, -ViewCubeStyle::OrthoExtent,
                                  ViewCubeStyle::OrthoExtent, 0.1, 10.0);
        const math::Mat4 invVP = (cubeProj * cubeView).inverse();

        math::Vec4 nearPt = invVP * math::Vec4(ndcX, ndcY, -1.0, 1.0);
        math::Vec4 farPt = invVP * math::Vec4(ndcX, ndcY, 1.0, 1.0);
        nearPt /= nearPt.w;
        farPt /= farPt.w;

        const math::Vec3 rayOrigin(nearPt.x, nearPt.y, nearPt.z);
        const math::Vec3 rayDir = (math::Vec3(farPt) - rayOrigin).normalized();

        double bestT = 1.0e100;
        ViewCubePart bestPart;
        auto intersectTriangle = [&](const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
                                     double& tOut) -> bool {
            const math::Vec3 e1 = b - a;
            const math::Vec3 e2 = c - a;
            const math::Vec3 p = rayDir.cross(e2);
            const double det = e1.dot(p);
            if (std::abs(det) < 1.0e-10) {
                return false;
            }

            const double invDet = 1.0 / det;
            const math::Vec3 tv = rayOrigin - a;
            const double u = tv.dot(p) * invDet;
            if (u < 0.0 || u > 1.0) {
                return false;
            }

            const math::Vec3 q = tv.cross(e1);
            const double v = rayDir.dot(q) * invDet;
            if (v < 0.0 || u + v > 1.0) {
                return false;
            }

            const double t = e2.dot(q) * invDet;
            if (t < 0.0) {
                return false;
            }
            tOut = t;
            return true;
        };

        auto testShape = [&](const ViewCubePartShape& shape) {
            double t = 0.0;
            bool matched = intersectTriangle(shape.vertices[0], shape.vertices[1], shape.vertices[2], t);
            if (!matched && shape.vertexCount == 4) {
                matched = intersectTriangle(shape.vertices[0], shape.vertices[2], shape.vertices[3], t);
            }
            if (!matched || t >= bestT) {
                return;
            }
            bestT = t;
            bestPart = shape.part;
        };

        for (const auto& part : parts()) {
            testShape(partShape(part));
        }

        if (bestT == 1.0e100) {
            hit.type = ViewCubePartType::None;
            return hit;
        }

        hit.type = bestPart.type;
        hit.face = faceForPart(bestPart);
        hit.part = bestPart;
        return hit;
    }

    ViewCubeHit pickFace(int32_t screenX, int32_t screenY, uint32_t viewportWidth, uint32_t viewportHeight,
                         const math::Mat4& mainViewMatrix) const {
        return pickPart(screenX, screenY, viewportWidth, viewportHeight, mainViewMatrix);
    }

private:
    ViewCubeLayout layout_;
};

}  // namespace mulan::engine
