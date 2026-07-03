/**
 * @file device.h
 * @brief RHI设备基类，GPU资源创建入口与帧循环接口
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "buffer.h"
#include "command_list.h"
#include "fence.h"
#include "pipeline_state.h"
#include "render_target.h"
#include "sampler.h"
#include "shader.h"
#include "swap_chain.h"
#include "texture.h"
#include <mulan/math/math.h>
#include "../window.h"

#include <mulan/core/result/error.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <memory>

namespace mulan::engine {

// ============================================================
// 后端类型
// ============================================================

enum class GraphicsBackend : uint8_t {
    OpenGL,
    Vulkan,
    D3D12,
    D3D11,
};

// ============================================================
// 设备能力信息（后端初始化后填充）
// ============================================================

struct GPUDeviceCapabilities {
    GraphicsBackend backend = GraphicsBackend::OpenGL;
    uint32_t maxTextureSize    = 0;
    uint32_t maxTextureAniso   = 0;
    uint32_t minUniformBufferOffsetAlignment = 256;
    bool     depthClamp        = false;
    bool     geometryShader    = false;
    bool     tessellationShader = false;
    bool     computeShader     = false;
};

// ============================================================
// 设备创建参数 — 跨后端通用
// ============================================================

struct DeviceCreateInfo {
    GraphicsBackend   backend          = GraphicsBackend::Vulkan;
    NativeWindowHandle window           = {};
    RenderConfig       renderConfig     = {};
    bool               enableValidation = true;
    const char*        appName          = "mulan";
};

// ============================================================
// RHI 设备基类
//
// 所有 GPU 资源的工厂 + 帧循环管理。
// 后端实现继承此类，UI 层只依赖此接口。
//
// 资源所有权约定：
//   createXxx() 返回 std::unique_ptr<X>，所有权转移给调用方。
//   资源析构自行销毁后端句柄（VK/D3D12 资源类在析构里释放）。
//   RHIDevice 必须在所有资源之后析构（由 owner 保证声明逆序）。
// ============================================================

class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // --- 工厂函数（根据 backend 创建具体实现）---
    static std::expected<std::shared_ptr<RHIDevice>, core::Error> create(const DeviceCreateInfo& ci);

    // --- 设备信息 ---

    virtual GraphicsBackend backend() const = 0;
    virtual const GPUDeviceCapabilities& capabilities() const = 0;
    virtual const RenderConfig& renderConfig() const = 0;

    // --- 裁剪空间修正 ---
    // 各后端 NDC 约定不同（Vulkan: Y↓ z∈[0,1]，OpenGL: Y↑ z∈[-1,1]）。
    // Camera / Mat4 统一生成标准右手坐标（Y↑ z∈[-1,1]），
    // 由后端提供修正矩阵：finalProj = clipCorrection * projection。
    // 上层无需关心后端差异。

    virtual Mat4 clipSpaceCorrectionMatrix() const = 0;

    // --- 资源创建 ---
    // 全部返回 std::expected<unique_ptr<T>, core::Error>：失败时调用方拿到
    // 失败原因（含 EngineErrorCode），可据此决策。参见 core/result/error.h。
    virtual std::expected<std::unique_ptr<Buffer>,        core::Error> createBuffer(const BufferDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<Texture>,       core::Error> createTexture(const TextureDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<Shader>,        core::Error> createShader(const ShaderDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<PipelineState>, core::Error> createPipelineState(const GraphicsPipelineDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<CommandList>,   core::Error> createCommandList() = 0;
    virtual std::expected<std::unique_ptr<SwapChain>,     core::Error> createSwapChain(const SwapChainDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<RenderTarget>,  core::Error> createRenderTarget(const RenderTargetDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<Sampler>,       core::Error> createSampler(const SamplerDesc& desc) = 0;
    virtual std::expected<std::unique_ptr<Fence>,         core::Error> createFence(uint64_t initialValue = 0) = 0;

    // --- 资源上传 ---
    // 把 CPU 端像素数据同步上传到 GPU 纹理，并在内部完成到 SHADER_READ 的状态转换。
    // 同步等待 GPU 完成。仅支持单 mip、非压缩颜色格式（bpp 由公共工具统一计算）。
    // 后端各自实现，经此接口避免向 render 层泄漏后端 UploadContext 类型。
    virtual void uploadTextureData(Texture* dst, const void* data,
                                   uint32_t width, uint32_t height,
                                   TextureFormat format) = 0;

    // --- 提交命令 ---

    virtual void executeCommandLists(CommandList** cmdLists,
                                     uint32_t count,
                                     Fence* fence = nullptr,
                                     uint64_t fenceValue = 0) = 0;

    void executeCommandList(CommandList* cmdList,
                            Fence* fence = nullptr,
                            uint64_t fenceValue = 0) {
        executeCommandLists(&cmdList, 1, fence, fenceValue);
    }

    // --- 等待 GPU 空闲 ---

    virtual void waitIdle() = 0;

    // ============================================================
    // 帧循环接口（UI 层的标准渲染流程）
    //
    //   device->beginFrame();
    //   auto* cmd = device->frameCommandList();
    //   cmd->begin();
    //   swapchain->beginRenderPass(cmd);
    //   // ... draw calls ...
    //   swapchain->endRenderPass(cmd);
    //   cmd->end();
    //   device->submitAndPresent(swapchain);
    // ============================================================

    /// 每帧开头：等待上一轮完成、acquire next image、重置资源
    /// swapchain 为 nullptr 表示离屏模式（不做 acquire）
    virtual void beginFrame(SwapChain* swapchain = nullptr) = 0;

    /// 清空内部缓存（swapchain resize 时调用）
    virtual void clearCaches() = 0;

    /// 获取当前帧的 CommandList（已 reset，可直接 begin）
    virtual CommandList* frameCommandList() = 0;

    /// 提交当前帧命令 + present
    /// 内部调用 submit() → present()
    virtual void submitAndPresent(SwapChain* swapchain) = 0;

    /// 提交当前帧命令（不 present，用于多线程录制场景）
    /// 调用后可继续用其他 cmd list + present()
    virtual void submit() = 0;

    /// 呈现 swapchain 到屏幕（与 submit 分离，支持多线程录制后统一 present）
    virtual void present(SwapChain* swapchain) = 0;

    /// 提交当前帧命令（无 present — 用于离屏渲染）
    virtual void submitOffscreen() = 0;

    // ============================================================
    // Descriptor 绑定（UBO / Texture 统一绑定接口）
    // ============================================================

protected:
    RHIDevice() = default;
    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;
};

// ============================================================
} // namespace mulan::engine
