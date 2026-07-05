#include "vk_fence.h"

#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"

#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<VKFence>> VKFence::create(vk::Device device, uint64_t initialValue) {
    auto obj = std::unique_ptr<VKFence>(new VKFence(device));

    vk::SemaphoreTypeCreateInfo timelineCI;
    timelineCI.semaphoreType = vk::SemaphoreType::eTimeline;
    timelineCI.initialValue = initialValue;

    vk::SemaphoreCreateInfo ci;
    ci.pNext = &timelineCI;

    try {
        obj->semaphore_ = device.createSemaphore(ci);
    } catch (const vk::Error& e) {
        return std::unexpected(makeError(EngineErrorCode::FenceCreateFailed,
                                         std::string("createSemaphore(timeline) failed: ") + e.what()));
    }

    return obj;
}

VKFence::~VKFence() {
    if (semaphore_) {
        device_.destroySemaphore(semaphore_);
    }
}

void VKFence::signal(uint64_t value) {
    vk::SemaphoreSignalInfo info;
    info.semaphore = semaphore_;
    info.value = value;
    device_.signalSemaphore(info);
}

void VKFence::wait(uint64_t value) {
    vk::SemaphoreWaitInfo info;
    info.semaphoreCount = 1;
    vk::Semaphore semaphores[] = { semaphore_ };
    uint64_t values[] = { value };
    info.pSemaphores = semaphores;
    info.pValues = values;
    device_.waitSemaphores(info, UINT64_MAX);
}

uint64_t VKFence::completedValue() const {
    return device_.getSemaphoreCounterValue(semaphore_);
}

}  // namespace mulan::engine
