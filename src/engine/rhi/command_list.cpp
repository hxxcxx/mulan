#include "command_list.h"
#include "sampler.h"

#include <algorithm>
#include <functional>

namespace mulan::engine {

void CommandList::resetResourceUsage() {
    used_resources_.clear();
}

void CommandList::recordResourceUse(RHITrackedResource* resource) {
    if (resource)
        used_resources_.push_back(resource);
}

void CommandList::recordBindGroupUse(BindGroup& group) {
    recordResourceUse(&group);
    const BindGroupEntry* entries = group.entries();
    for (uint8_t i = 0; i < group.entryCount(); ++i) {
        recordResourceUse(entries[i].buffer);
        recordResourceUse(entries[i].texture);
        recordResourceUse(entries[i].sampler);
    }
}

void CommandList::recordBindGroupUse(const BindGroupDesc& desc) {
    for (uint8_t i = 0; i < desc.count; ++i) {
        recordResourceUse(desc.entries[i].buffer);
        recordResourceUse(desc.entries[i].texture);
        recordResourceUse(desc.entries[i].sampler);
    }
}

void CommandList::recordRenderPassUse(const RenderPassBeginInfo& info) {
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        recordResourceUse(info.colorAttachments[i].target);
        recordResourceUse(info.colorAttachments[i].resolveTarget);
    }
    recordResourceUse(info.depthAttachment.target);
    recordResourceUse(info.depthAttachment.resolveTarget);
}

void CommandList::markSubmitted(SubmissionToken token) {
    if (!token)
        return;

    std::sort(used_resources_.begin(), used_resources_.end(), std::less<>{});
    const auto uniqueEnd = std::unique(used_resources_.begin(), used_resources_.end());
    for (auto it = used_resources_.begin(); it != uniqueEnd; ++it)
        (*it)->markUsed(token);
    markUsed(token);
}

}  // namespace mulan::engine
