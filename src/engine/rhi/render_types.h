/**
 * @file render_types.h
 * @brief 渲染辅助类型：视口、裁剪矩形、绘制参数、资源状态
 * @author hxxcxx
 * @date 2026-04-17
 */

#pragma once

#include <mulan/graphics/vertex/vertex_layout.h>

#include <cstdint>

namespace mulan::engine {

using graphics::IndexType;

// ============================================================
// 前向声明
// ============================================================

class Texture;

// ============================================================
// RenderPass 加载/存储操作
// ============================================================

enum class LoadAction : uint8_t {
    Clear = 0,     // 开始时清除
    Load = 1,      // 开始时加载上一帧内容
    DontCare = 2,  // 内容未定义
};

enum class StoreAction : uint8_t {
    Store = 0,     // 结束时存储内容
    DontCare = 1,  // 结束时内容不需要
};

// ============================================================
// RenderPass 附件信息
// ============================================================

struct RenderPassAttachmentInfo {
    Texture* target = nullptr;
    Texture* resolveTarget = nullptr;
    LoadAction loadAction = LoadAction::Clear;
    StoreAction storeAction = StoreAction::Store;
};

// ============================================================
// CommandList::beginRenderPass 的参数
// ============================================================

struct RenderPassBeginInfo {
    static constexpr uint8_t kMaxColorTargets = 8;

    RenderPassAttachmentInfo colorAttachments[kMaxColorTargets] = {};
    uint8_t colorCount = 0;
    RenderPassAttachmentInfo depthAttachment = {};  // target==nullptr 表示无深度

    float clearColor[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;

    bool presentSource = false;  // true = swapchain 模式，VK finalLayout=PresentSrcKHR
    uint32_t width = 0;
    uint32_t height = 0;
};

// ============================================================
// 视口与裁剪矩形
// ============================================================

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
};

// ============================================================
// 绘制参数
// ============================================================

struct DrawAttribs {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t startVertex = 0;
    uint32_t startInstance = 0;
};

struct DrawIndexedAttribs {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t startIndex = 0;
    int32_t baseVertex = 0;
    uint32_t startInstance = 0;
    IndexType indexType = IndexType::UInt32;
};

// ============================================================
// 资源状态转换（VK/D3D12 需要，GL 可忽略）
// ============================================================

enum class ResourceState : uint32_t {
    Common = 0,
    VertexBuffer = 1,
    IndexBuffer = 2,
    UniformBuffer = 3,
    ShaderResource = 4,
    UnorderedAccess = 5,
    RenderTarget = 6,
    DepthWrite = 7,
    DepthRead = 8,
    Present = 9,
    CopyDest = 10,
    CopySrc = 11,
};

enum class ResourceTransitionMode : uint8_t {
    None,        // 不转换（GL 后端常用）
    Transition,  // 自动转换
};

}  // namespace mulan::engine
