#include "capture_batch.h"

#include <utility>

namespace mulan::view {

void CaptureBatch::add(CaptureRequest request) {
    requests_.push_back(std::move(request));
}

void CaptureBatch::clear() {
    requests_.clear();
}

}  // namespace mulan::view
