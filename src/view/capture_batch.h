/**
 * @file capture_batch.h
 * @brief CaptureBatch 管理一组按不同相机和渲染风格执行的截图请求。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "capture_request.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mulan::view {

class CaptureBatch {
public:
    void add(CaptureRequest request);
    void clear();

    bool empty() const { return requests_.empty(); }
    size_t size() const { return requests_.size(); }

    std::span<const CaptureRequest> requests() const { return requests_; }

private:
    std::vector<CaptureRequest> requests_;
};

} // namespace mulan::view
