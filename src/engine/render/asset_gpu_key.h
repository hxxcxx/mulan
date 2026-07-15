/**
 * @file asset_gpu_key.h
 * @brief 定义带资源域、来源、子资源和类型的持久 GPU 资源键
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include <cstdint>
#include <functional>

namespace mulan::engine {

struct ResourceDomainId {
    uint64_t value = 0;

    constexpr explicit operator bool() const { return value != 0; }
    constexpr bool operator==(const ResourceDomainId&) const = default;
};

enum class RenderResourceKind : uint8_t {
    Legacy,
    Geometry,
    Texture,
    Material,
    PreviewGeometry,
    PreviewMaterial,
    Builtin,
};

struct RenderResourceKey {
    ResourceDomainId domain;
    uint64_t source = 0;
    uint32_t subresource = 0;
    RenderResourceKind kind = RenderResourceKind::Legacy;

    constexpr explicit operator bool() const { return domain && source != 0; }
    constexpr bool operator==(const RenderResourceKey&) const = default;
};

constexpr RenderResourceKey makeRenderResourceKey(ResourceDomainId domain, uint64_t source, RenderResourceKind kind,
                                                  uint32_t subresource = 0) {
    return RenderResourceKey{ domain, source, subresource, kind };
}

constexpr bool isGeometryResourceKey(const RenderResourceKey& key) {
    return key && (key.kind == RenderResourceKind::Geometry || key.kind == RenderResourceKind::PreviewGeometry ||
                   key.kind == RenderResourceKind::Legacy);
}

constexpr bool isTextureResourceKey(const RenderResourceKey& key) {
    return key && (key.kind == RenderResourceKind::Texture || key.kind == RenderResourceKind::Legacy);
}

/// 资产库身份到文档资源域的一一映射；高位命名空间避免与内置、预览域混淆。
constexpr ResourceDomainId resourceDomainForAssetLibrary(uint64_t assetLibraryDomain) {
    return ResourceDomainId{ 0x1000000000000000ull | (assetLibraryDomain & 0x0fffffffffffffffull) };
}

constexpr ResourceDomainId builtinResourceDomain() {
    return ResourceDomainId{ 0xf000000000000001ull };
}

/// 为 PreviewLayer/工具等非文档资源分配进程内唯一域。
ResourceDomainId allocateTransientResourceDomain();

// 兼容低层独立测试和渐进迁移入口；生产投影必须使用 makeRenderResourceKey 显式指定资源域。
using AssetGpuKey = RenderResourceKey;
inline constexpr AssetGpuKey makeAssetGpuKey(uint64_t value) {
    return makeRenderResourceKey(ResourceDomainId{ 0xe000000000000001ull }, value, RenderResourceKind::Legacy);
}

}  // namespace mulan::engine

template <>
struct std::hash<mulan::engine::ResourceDomainId> {
    size_t operator()(mulan::engine::ResourceDomainId domain) const noexcept {
        return std::hash<uint64_t>{}(domain.value);
    }
};

template <>
struct std::hash<mulan::engine::RenderResourceKey> {
    size_t operator()(const mulan::engine::RenderResourceKey& key) const noexcept {
        size_t value = std::hash<uint64_t>{}(key.domain.value);
        const auto combine = [&value](size_t part) {
            value ^= part + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
        };
        combine(std::hash<uint64_t>{}(key.source));
        combine(std::hash<uint32_t>{}(key.subresource));
        combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.kind)));
        return value;
    }
};
