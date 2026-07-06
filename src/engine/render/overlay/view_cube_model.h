#pragma once

#include <cstdint>

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

class ViewCubeModel {
public:
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

private:
    ViewCubeLayout layout_;
};

}  // namespace mulan::engine
