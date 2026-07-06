#pragma once

#include <mulan/math/math.h>

#include <algorithm>
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

struct ViewCubeRect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    bool contains(int32_t px, int32_t py) const { return px >= x && py >= y && px < x + width && py < y + height; }
};

struct ViewCubeLayout {
    uint32_t size = 148;
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
    int32_t localX = 0;
    int32_t localY = 0;

    explicit operator bool() const { return type != ViewCubePartType::None; }
};

struct ViewCubeInteractionState {
    ViewCubeFace hoveredFace = ViewCubeFace::Front;
    ViewCubeFace pressedFace = ViewCubeFace::Front;
    bool hasHoveredFace = false;
    bool hasPressedFace = false;
};

class ViewCubeModel {
public:
    static constexpr double kCubeHalfExtent = 0.58;
    static constexpr double kOrthoExtent = 1.36;

    explicit ViewCubeModel(ViewCubeLayout layout = {}) : layout_(layout) {}

    const ViewCubeLayout& layout() const { return layout_; }
    void setLayout(const ViewCubeLayout& layout) { layout_ = layout; }

    ViewCubeRect viewportRect(uint32_t viewportWidth, uint32_t viewportHeight) const {
        return layout_.rect(viewportWidth, viewportHeight);
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

    ViewCubeHit pickFace(int32_t screenX, int32_t screenY, uint32_t viewportWidth, uint32_t viewportHeight,
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
        cubeView[3] = math::Vec4(0, 0, -3.5, 1);
        const math::Mat4 cubeProj =
                math::Mat4::ortho(-kOrthoExtent, kOrthoExtent, -kOrthoExtent, kOrthoExtent, 0.1, 10.0);
        const math::Mat4 invVP = (cubeProj * cubeView).inverse();

        math::Vec4 nearPt = invVP * math::Vec4(ndcX, ndcY, -1.0, 1.0);
        math::Vec4 farPt = invVP * math::Vec4(ndcX, ndcY, 1.0, 1.0);
        nearPt /= nearPt.w;
        farPt /= farPt.w;

        const math::Vec3 rayOrigin(nearPt.x, nearPt.y, nearPt.z);
        const math::Vec3 rayDir = (math::Vec3(farPt) - rayOrigin).normalized();

        double bestT = 1.0e100;
        ViewCubeFace bestFace = ViewCubeFace::Front;
        auto testPlane = [&](double plane, int axis, ViewCubeFace face) {
            const double dir = rayDir[axis];
            if (std::abs(dir) < 1.0e-9) {
                return;
            }
            const double t = (plane - rayOrigin[axis]) / dir;
            if (t < 0.0 || t >= bestT) {
                return;
            }
            const math::Vec3 p = rayOrigin + rayDir * t;
            constexpr double eps = 1.0e-5;
            if (p.x < -kCubeHalfExtent - eps || p.x > kCubeHalfExtent + eps || p.y < -kCubeHalfExtent - eps ||
                p.y > kCubeHalfExtent + eps || p.z < -kCubeHalfExtent - eps || p.z > kCubeHalfExtent + eps) {
                return;
            }
            bestT = t;
            bestFace = face;
        };

        testPlane(kCubeHalfExtent, 2, ViewCubeFace::Front);
        testPlane(-kCubeHalfExtent, 2, ViewCubeFace::Back);
        testPlane(-kCubeHalfExtent, 0, ViewCubeFace::Left);
        testPlane(kCubeHalfExtent, 0, ViewCubeFace::Right);
        testPlane(kCubeHalfExtent, 1, ViewCubeFace::Top);
        testPlane(-kCubeHalfExtent, 1, ViewCubeFace::Bottom);

        if (bestT == 1.0e100) {
            hit.type = ViewCubePartType::None;
            return hit;
        }

        hit.type = ViewCubePartType::Face;
        hit.face = bestFace;
        return hit;
    }

private:
    ViewCubeLayout layout_;
};

}  // namespace mulan::engine
