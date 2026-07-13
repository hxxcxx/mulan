#include "device.h"
#include "engine_error_code.h"

#include <mulan/core/log/log.h>

#include <algorithm>
#include <cassert>
#include <atomic>

namespace mulan::engine {

namespace {
std::atomic<uint64_t> g_next_device_generation{ 1 };

void runDeferredRelease(RHIDevice::DeferredRelease& release) noexcept {
    try {
        release();
    } catch (const std::exception& error) {
        LOG_ERROR("[RHI] Deferred release callback failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[RHI] Deferred release callback failed with an unknown error");
    }
}
}  // namespace

RHIDevice::RHIDevice() : device_generation_(g_next_device_generation.fetch_add(1, std::memory_order_relaxed)) {
}

RHIDevice::~RHIDevice() {
    assertNoLiveResources();
    detachLiveResources();
}

void RHIDevice::initializeSubmissionTracking(std::unique_ptr<Fence> fence) {
    submission_fence_ = std::move(fence);
    next_submission_value_.store(0, std::memory_order_relaxed);
    last_submission_value_.store(0, std::memory_order_relaxed);
}

void RHIDevice::shutdownSubmissionTracking() {
    submission_fence_.reset();
}

SubmissionToken RHIDevice::reserveSubmissionToken(QueueType queue) {
    if (!submission_fence_ || queue != QueueType::Graphics)
        return {};
    return SubmissionToken{ device_generation_, queue,
                            next_submission_value_.fetch_add(1, std::memory_order_relaxed) + 1 };
}

void RHIDevice::commitSubmission(SubmissionToken token) {
    if (!token || token.deviceGeneration != device_generation_ || token.queue != QueueType::Graphics)
        return;

    uint64_t current = last_submission_value_.load(std::memory_order_relaxed);
    while (current < token.value &&
           !last_submission_value_.compare_exchange_weak(current, token.value, std::memory_order_release,
                                                         std::memory_order_relaxed)) {}
}

SubmissionToken RHIDevice::lastSubmissionToken(QueueType queue) const {
    if (queue != QueueType::Graphics || !submission_fence_)
        return {};
    const uint64_t value = last_submission_value_.load(std::memory_order_acquire);
    return value == 0 ? SubmissionToken{} : SubmissionToken{ device_generation_, queue, value };
}

bool RHIDevice::isSubmissionComplete(SubmissionToken token) const {
    if (!token || token.deviceGeneration != device_generation_ || token.queue != QueueType::Graphics ||
        !submission_fence_)
        return false;
    try {
        return submission_fence_->completedValue() >= token.value;
    } catch (const std::exception& error) {
        LOG_ERROR("[RHI] Submission completion query failed: {}", error.what());
        return false;
    }
}

core::Result<void> RHIDevice::waitForSubmission(SubmissionToken token) {
    if (!token || token.deviceGeneration != device_generation_ || token.queue != QueueType::Graphics ||
        !submission_fence_) {
        return std::unexpected(
                makeError(EngineErrorCode::InvalidSubmissionToken, "submission token does not belong to this device"));
    }
    try {
        submission_fence_->wait(token.value);
        if (submission_fence_->completedValue() < token.value) {
            return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed,
                                             "GPU wait returned before the requested submission completed"));
        }
    } catch (const std::exception& error) {
        return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, error.what()));
    }
    return {};
}

core::Result<void> RHIDevice::retire(SubmissionToken token, DeferredRelease release) {
    if (!release)
        return {};
    if (!token || token.deviceGeneration != device_generation_ || token.queue != QueueType::Graphics ||
        !submission_fence_) {
        return std::unexpected(makeError(EngineErrorCode::InvalidSubmissionToken,
                                         "deferred release token does not belong to this device"));
    }

    if (isSubmissionComplete(token)) {
        runDeferredRelease(release);
        return {};
    }

    std::scoped_lock lock(deferred_release_mutex_);
    deferred_releases_.push_back(DeferredReleaseEntry{ token, std::move(release) });
    return {};
}

void RHIDevice::collectGarbage() {
    std::vector<DeferredRelease> ready;
    {
        std::scoped_lock lock(deferred_release_mutex_);
        auto it = deferred_releases_.begin();
        while (it != deferred_releases_.end()) {
            if (isSubmissionComplete(it->token)) {
                ready.push_back(std::move(it->release));
                it = deferred_releases_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& release : ready)
        runDeferredRelease(release);
}

void RHIDevice::drainDeferredReleases() {
    std::vector<DeferredRelease> releases;
    {
        std::scoped_lock lock(deferred_release_mutex_);
        releases.reserve(deferred_releases_.size());
        for (auto& entry : deferred_releases_)
            releases.push_back(std::move(entry.release));
        deferred_releases_.clear();
    }
    for (auto& release : releases)
        runDeferredRelease(release);
}

void RHIDevice::registerLiveResource(const RHITrackedResource* resource, RHIResourceKind kind, std::string_view name) {
    if (!resource) {
        return;
    }

    std::scoped_lock lock(live_resources_mutex_);
    auto it = std::find_if(live_resources_.begin(), live_resources_.end(),
                           [&](const LiveResourceInfo& info) { return info.resource == resource; });
    if (it != live_resources_.end()) {
        it->kind = kind;
        it->name = std::string(name);
        return;
    }
    live_resources_.push_back(LiveResourceInfo{ resource, kind, std::string(name) });
}

void RHIDevice::unregisterLiveResource(const RHITrackedResource* resource) {
    if (!resource) {
        return;
    }

    std::scoped_lock lock(live_resources_mutex_);
    auto it = std::remove_if(live_resources_.begin(), live_resources_.end(),
                             [&](const LiveResourceInfo& info) { return info.resource == resource; });
    live_resources_.erase(it, live_resources_.end());
}

bool RHIDevice::hasLiveResources() const {
    std::scoped_lock lock(live_resources_mutex_);
    return !live_resources_.empty();
}

void RHIDevice::dumpLiveResources() const {
    std::scoped_lock lock(live_resources_mutex_);
    if (live_resources_.empty()) {
        return;
    }

    LOG_WARN("[RHI] Device destruction detected {} live resource(s)", live_resources_.size());
    for (const auto& info : live_resources_) {
        const auto kindName = toString(info.kind);
        if (!info.name.empty()) {
            LOG_WARN("[RHI] Live resource: kind={}, address={}, name={}", kindName,
                     static_cast<const void*>(info.resource), info.name);
        } else {
            LOG_WARN("[RHI] Live resource: kind={}, address={}", kindName, static_cast<const void*>(info.resource));
        }
    }
}

void RHIDevice::assertNoLiveResources() const {
    if (!hasLiveResources()) {
        return;
    }
    dumpLiveResources();
    assert(false && "RHIDevice destroyed while RHI resources are still alive");
}

void RHIDevice::detachLiveResources() {
    std::vector<const RHITrackedResource*> resources;
    {
        std::scoped_lock lock(live_resources_mutex_);
        resources.reserve(live_resources_.size());
        for (const auto& info : live_resources_) {
            resources.push_back(info.resource);
        }
        live_resources_.clear();
    }

    for (const auto* resource : resources) {
        if (resource) {
            const_cast<RHITrackedResource*>(resource)->detachFromDevice(*this);
        }
    }
}

}  // namespace mulan::engine
