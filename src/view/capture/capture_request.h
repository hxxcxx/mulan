/**
 * @file capture_request.h
 * @brief CaptureRequest 描述 view 层一次截图所需的相机、渲染风格和输出参数。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../core/view_state.h"

#include <mulan/render/camera/camera.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/core/result/error.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace mulan::view {

enum class CaptureRenderStyle {
    Shaded,
    ShadedWithEdges,
    Wireframe,
    EdgesOnly,
};

struct CaptureVisual {
    CaptureRenderStyle style = CaptureRenderStyle::ShadedWithEdges;
    bool showViewCube = false;
    bool showOverlays = false;
};

struct CaptureRequest {
    std::string name;
    engine::RenderCaptureDesc desc;
    engine::Camera camera;
    CaptureVisual visual;
};

struct CaptureImage {
    std::string name;
    engine::RenderCaptureResult result;
};

enum class CaptureFailureCode : uint8_t {
    None,
    ContextNotInitialized,
    InvalidSize,
    CaptureFailed,
};

struct CaptureResult {
    std::string name;
    Result<engine::RenderCaptureResult> result;
    CaptureFailureCode failure = CaptureFailureCode::None;
    std::string message;

    bool succeeded() const { return result.has_value(); }
};

struct CaptureBatchResult {
    std::vector<CaptureResult> items;

    bool empty() const { return items.empty(); }
    bool allSucceeded() const;
    std::size_t succeededCount() const;
    std::size_t failedCount() const;
    std::vector<CaptureImage> images() const;
};

ViewState makeCaptureViewState(const engine::Camera& camera, const CaptureVisual& visual, uint32_t width,
                               uint32_t height);

}  // namespace mulan::view
