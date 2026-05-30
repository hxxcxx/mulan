#include "VKFence.h"

namespace mulan::engine {

VKFence::VKFence(vk::Device device, uint64_t initialValue)
    : m_device(device)
{
    vk::SemaphoreTypeCreateInfo timelineCI;
    timelineCI.semaphoreType = vk::SemaphoreType::eTimeline;
    timelineCI.initialValue  = initialValue;

    vk::SemaphoreCreateInfo ci;
    ci.pNext = &timelineCI;

    m_semaphore = m_device.createSemaphore(ci);
}

VKFence::~VKFence() {
    if (m_semaphore) {
        m_device.destroySemaphore(m_semaphore);
    }
}

void VKFence::signal(uint64_t value) {
    vk::SemaphoreSignalInfo info;
    info.semaphore = m_semaphore;
    info.value     = value;
    m_device.signalSemaphore(info);
}

void VKFence::wait(uint64_t value) {
    vk::SemaphoreWaitInfo info;
    info.semaphoreCount = 1;
    vk::Semaphore semaphores[] = { m_semaphore };
    uint64_t values[] = { value };
    info.pSemaphores = semaphores;
    info.pValues     = values;
    m_device.waitSemaphores(info, UINT64_MAX);
}

uint64_t VKFence::completedValue() const {
    return m_device.getSemaphoreCounterValue(m_semaphore);
}

} // namespace mulan::engine
