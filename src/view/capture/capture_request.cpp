#include "capture_request.h"
namespace mulan::view {

bool CaptureBatchResult::allSucceeded() const {
    return failedCount() == 0;
}

std::size_t CaptureBatchResult::succeededCount() const {
    std::size_t count = 0;
    for (const auto& item : items) {
        if (item.succeeded()) {
            ++count;
        }
    }
    return count;
}

std::size_t CaptureBatchResult::failedCount() const {
    return items.size() - succeededCount();
}

std::vector<CaptureImage> CaptureBatchResult::images() const {
    std::vector<CaptureImage> result;
    result.reserve(succeededCount());
    for (const auto& item : items) {
        if (item.result) {
            result.push_back(CaptureImage{ .name = item.name, .result = *item.result });
        }
    }
    return result;
}

}  // namespace mulan::view
