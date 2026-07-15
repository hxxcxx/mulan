/**
 * @file asset_gpu_key.cpp
 * @brief GPU 临时资源域分配实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "asset_gpu_key.h"

#include <atomic>

namespace mulan::engine {

ResourceDomainId allocateTransientResourceDomain() {
    static std::atomic<uint64_t> next{ 1 };
    uint64_t value = next.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) {
        value = next.fetch_add(1, std::memory_order_relaxed);
    }
    return ResourceDomainId{ 0x2000000000000000ull | (value & 0x0fffffffffffffffull) };
}

}  // namespace mulan::engine
