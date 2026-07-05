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

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace mulan::view {

class ViewContext;

class CaptureService {
public:
    core::Result<engine::RenderCaptureResult> capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const;

    core::Result<CaptureImage> capture(ViewContext& context, const CaptureRequest& request) const;

    CaptureBatchResult
    capture(ViewContext& context, const CaptureBatch& batch) const;

private:
    class CaptureScope;

    static uint32_t captureWidth(ViewContext& context, const engine::RenderCaptureDesc& desc);
    static uint32_t captureHeight(ViewContext& context, const engine::RenderCaptureDesc& desc);

    static std::optional<CaptureResult>
    validateCaptureInput(ViewContext& context,
                         std::string name,
                         uint32_t width,
                         uint32_t height);

    static std::optional<CaptureResult>
    configureCaptureTarget(ViewContext& context,
                           const engine::RenderCaptureDesc& desc,
                           std::string name,
                           uint32_t width,
                           uint32_t height);

    static core::Result<engine::RenderCaptureResult> readCaptureResult(ViewContext& context,
                      const engine::RenderCaptureDesc& desc,
                      uint32_t width,
                      uint32_t height);
};

} // namespace mulan::view
