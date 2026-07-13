#include "device.h"

#include <mulan/core/log/log.h>

#include <algorithm>
#include <cassert>

namespace mulan::engine {

RHIDevice::~RHIDevice() {
    assertNoLiveResources();
    detachLiveResources();
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
