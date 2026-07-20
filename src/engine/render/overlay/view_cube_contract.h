/**
 * @file view_cube_contract.h
 * @brief 定义 ViewCube 在 view 与 renderer 之间共享的值类型和确定性几何。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <mulan/math/math.h>

#include <array>
#include <cstdint>

namespace mulan::engine {

enum class ViewCubeCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
enum class ViewCubePartType : uint8_t { None, Face, Edge, Corner };
enum class ViewCubeFace : uint8_t { Front, Back, Left, Right, Top, Bottom };

struct ViewCubePart {
    ViewCubePartType type = ViewCubePartType::None;
    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;
    uint32_t index = 0;
    friend constexpr bool operator==(const ViewCubePart&, const ViewCubePart&) = default;
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
    static constexpr float AxisOrigin = -0.64f;
    static constexpr float AxisEnd = 0.62f;
    static constexpr float AxisConeLength = 0.14f;
    static constexpr float AxisShaftRadius = 0.020f;
    static constexpr float AxisConeRadius = 0.058f;
};

struct ViewCubeLayout {
    uint32_t size = ViewCubeStyle::ViewportSize;
    uint32_t margin = 16;
    ViewCubeCorner corner = ViewCubeCorner::BottomRight;
    ViewCubeRect rect(uint32_t viewportWidth, uint32_t viewportHeight) const;
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

class ViewCubeGeometry {
public:
    static constexpr uint32_t kPartCount = 26;
    static const std::array<ViewCubePart, kPartCount>& parts();
    static math::Vec3 partNormal(const ViewCubePart& part);
    static ViewCubeFace faceForPart(const ViewCubePart& part);
    static ViewCubePartShape partShape(const ViewCubePart& part);
};

}  // namespace mulan::engine
