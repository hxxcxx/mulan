/**
 * @file capture_service.h
 * @brief CaptureService 将 ViewContext 的离屏渲染与像素读回封装为正式截图入口。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "capture_batch.h"
#include <mulan/engine/render/frontend/render_capture.h>

#include <optional>
#include <vector>

namespace mulan::view {

class ViewContext;

class CaptureService {
public:
    std::optional<engine::RenderCaptureResult>
    capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const;

    std::optional<CaptureImage>
    capture(ViewContext& context, const CaptureRequest& request) const;

    std::vector<CaptureImage>
    capture(ViewContext& context, const CaptureBatch& batch) const;
};

} // namespace mulan::view
