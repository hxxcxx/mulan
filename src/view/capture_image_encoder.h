/**
 * @file capture_image_encoder.h
 * @brief CaptureImageEncoder 将截图读回结果转换为 core::Image 并导出 PNG。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "capture_request.h"

#include <mulan/core/image/image.h>
#include <mulan/core/result/error.h>
#include <mulan/engine/render/frontend/render_capture.h>

#include <expected>
#include <memory>
#include <string_view>

namespace mulan::view {

class CaptureImageEncoder {
public:
    static core::Result<std::shared_ptr<core::Image>> toImage(const engine::RenderCaptureResult& result);

    static core::Result<void> savePNG(const engine::RenderCaptureResult& result, std::string_view path);

    static core::Result<void> savePNG(const CaptureImage& image, std::string_view path);
};

}  // namespace mulan::view
