/**
 * @file capture_service.h
 * @brief CaptureService 将 ViewContext 的离屏渲染与像素读回封装为正式截图入口。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <mulan/engine/render/frontend/render_capture.h>

#include <optional>

namespace mulan::view {

class ViewContext;

class CaptureService {
public:
    std::optional<engine::RenderCaptureResult>
    capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const;
};

} // namespace mulan::view
