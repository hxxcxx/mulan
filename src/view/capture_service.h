/**
 * @file capture_service.h
 * @brief CaptureService 将 ViewContext 的离屏渲染与像素读回封装为正式截图入口。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "capture_batch.h"
#include <mulan/core/result/error.h>
#include <mulan/engine/render/frontend/render_capture.h>

#include <expected>
#include <vector>

namespace mulan::view {

class ViewContext;

class CaptureService {
public:
    std::expected<engine::RenderCaptureResult, core::Error>
    capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const;

    std::expected<CaptureImage, core::Error>
    capture(ViewContext& context, const CaptureRequest& request) const;

    CaptureBatchResult
    capture(ViewContext& context, const CaptureBatch& batch) const;
};

} // namespace mulan::view
