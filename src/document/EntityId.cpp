/**
 * @file EntityId.cpp
 * @brief EntityId 实现
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "EntityId.h"

#include <atomic>

namespace mulan::document {

EntityId EntityId::generate() {
    static std::atomic<uint64_t> counter{1};
    return EntityId{counter.fetch_add(1, std::memory_order_relaxed)};
}

} // namespace mulan::Document
