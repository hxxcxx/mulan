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
#include "submission.h"
#include "texture.h"
#include "bind_group.h"
#include <mulan/math/math.h>
#include "window.h"

#include <mulan/core/result/error.h>

#include <cstdint>
#include <atomic>
#include <cassert>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::engine {

// ============================================================
// 后端类型
// ============================================================

enum class GraphicsBackend : uint8_t { OpenGL, Vulkan, D3D11, D3D12 };

// ============================================================
// 设备能力信息（后端初始化后填充）
// ============================================================

struct GPUDeviceCapabilities {
    GraphicsBackend backend = GraphicsBackend::OpenGL;
    uint32_t maxTextureSize = 0;
    uint32_t maxTextureAniso = 0;
    uint32_t maxSampleCount = 1;
    uint32_t minUniformBufferOffsetAlignment = 256;
    uint32_t maxUniformBufferBindingSize = 64 * 1024;
    /// Capability 仅在公开接口具备完整可用实现时为 true，不能只反映底层 API/硬件能力。
    bool geometryShader = false;
    bool computeShader = false;
    bool indirectDraw = false;
    bool indirectDispatch = false;
    bool pushConstants = false;
};

// ============================================================
// 设备创建参数 — 跨后端通用
// ============================================================

struct DeviceCreateInfo {
    GraphicsBackend backend = GraphicsBackend::Vulkan;
    NativeWindowHandle window = {};
    RenderConfig renderConfig = {};
    bool enableValidation = true;
    const char* appName = "mulan";
};

/// 验证显式离屏渲染目标描述。创建接口不得静默修改调用方请求。
ResultVoid validateRenderTargetDesc(const RenderTargetDesc& desc, const GPUDeviceCapabilities& capabilities);

// ============================================================
// RHI 设备基类
//
// 所有 GPU 资源的工厂 + 帧循环管理。
//
// 资源所有权约定：
//   createXxx() 返回 std::unique_ptr<X>，所有权转移给调用方。
//   资源析构自行销毁后端句柄（VK/D3D12 资源类在析构里释放）。
//   RHIDevice 必须在所有资源之后析构（由 owner 保证声明逆序）。
// ============================================================

class RHIDevice {
public:
    virtual ~RHIDevice();

    // --- 工厂函数（根据 backend 创建具体实现）---
    static Result<std::unique_ptr<RHIDevice>> create(const DeviceCreateInfo& ci);

    void registerLiveResource(const RHITrackedResource* resource, RHIResourceKind kind, std::string_view name);
    void unregisterLiveResource(const RHITrackedResource* resource);
    bool hasLiveResources() const;
    void dumpLiveResources() const;

    // --- 设备信息 ---

    virtual GraphicsBackend backend() const = 0;
    virtual const GPUDeviceCapabilities& capabilities() const = 0;
    virtual const RenderConfig& renderConfig() const = 0;
    uint64_t deviceGeneration() const { return device_generation_; }
    SubmissionToken lastSubmissionToken() const;

    // --- 裁剪空间修正 ---
    // 各后端 NDC 约定不同（Vulkan: Y↓ z∈[0,1]，OpenGL: Y↑ z∈[-1,1]）。
    // Camera / math::Mat4 统一生成标准右手坐标（Y↑ z∈[-1,1]），
    // 由后端提供修正矩阵：finalProj = clipCorrection * projection。
    // 上层无需关心后端差异。

    virtual math::Mat4 clipSpaceCorrectionMatrix() const = 0;

    // --- 资源创建 ---
    // 全部返回 Result<unique_ptr<T>>：失败时调用方拿到
    // 失败原因（含 EngineErrorCode），可据此决策。参见 core/result/error.h。
    virtual Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) = 0;
    virtual Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) = 0;
    virtual Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc) = 0;
    virtual Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc) = 0;
    virtual Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(
            const ComputePipelineDesc& desc) = 0;
    virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
    virtual Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc& desc) = 0;
    virtual Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc& desc) = 0;
    virtual Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc& desc) = 0;
    virtual Result<std::unique_ptr<Fence>> createFence(uint64_t initialValue = 0) = 0;

    /// 创建 BindGroup 对象（从 layout + desc，缓存后端 descriptor 句柄）。
    /// layout 从 PipelineState::bindGroupLayout() 获取。
    virtual Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout& layout,
                                                               const BindGroupDesc& desc) = 0;

    // --- 资源上传 ---
    // 把 CPU 端像素数据同步上传到 GPU 纹理，并在内部完成到 SHADER_READ 的状态转换。
    // 同步等待 GPU 完成。仅支持单 mip、非压缩颜色格式（bpp 由公共工具统一计算）。
    // 后端各自实现，经此接口避免向 render 层泄漏后端 UploadContext 类型。
    virtual ResultVoid uploadTextureData(Texture* dst, const TextureUploadDesc& upload) = 0;

    /// 批量刷新所有待上传资源（beginUpload → 所有 pending upload → flushUploadBatch）。
    /// 在批量加载大量资源后调用一次，替代每个资源单独的 submit+wait。
    virtual ResultVoid beginUploadBatch() = 0;
    virtual ResultVoid flushUploadBatch() = 0;

    // --- 提交命令 ---

    virtual Result<SubmissionToken> executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence = nullptr,
                                                        uint64_t fenceValue = 0) = 0;

    Result<SubmissionToken> executeCommandList(CommandList* cmdList, Fence* fence = nullptr, uint64_t fenceValue = 0) {
        return executeCommandLists(&cmdList, 1, fence, fenceValue);
    }

    // --- 等待 GPU 空闲 ---

    virtual ResultVoid waitIdle() = 0;

    /// 查询/等待由本设备返回的提交标识。旧设备或其他设备的 token 会被拒绝。
    bool isSubmissionComplete(SubmissionToken token) const;
    ResultVoid waitForSubmission(SubmissionToken token);

    using DeferredRelease = std::move_only_function<void()>;

    /// 在 token 完成后于渲染线程执行释放回调。
    ResultVoid retire(SubmissionToken token, DeferredRelease release);
    void collectGarbage();

    /// 开始一帧并返回本帧 CommandList。swapchain 为空表示离屏渲染。
    virtual Result<CommandList*> beginFrame(SwapChain* swapchain = nullptr) = 0;

    /// 提交本帧；swapchain 非空时在提交成功后呈现。
    virtual Result<SubmissionToken> endFrame(SwapChain* swapchain = nullptr) = 0;

protected:
    RHIDevice();
    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;

    void assertResourceOwned(const RHITrackedResource* resource) const noexcept {
#ifndef NDEBUG
        assert(resource != nullptr);
        assert(!resource->isTracked() || resource->belongsTo(*this));
#else
        (void) resource;
#endif
    }
    void assertNoLiveResources() const;
    void detachLiveResources();
    ResultVoid waitForResourceLastUse(RHITrackedResource* resource) {
        return resource ? resource->waitForLastUse() : ResultVoid{};
    }

    void initializeSubmissionTracking(std::unique_ptr<Fence> fence);
    void shutdownSubmissionTracking();
    void drainDeferredReleases();
    ResultVoid validateCommandListsForSubmission(CommandList** commandLists, uint32_t count) const;
    SubmissionToken reserveSubmissionToken();
    void commitSubmission(SubmissionToken token);
    Fence* submissionFence() const { return submission_fence_.get(); }
    std::unique_lock<std::mutex> lockSubmissionQueue() { return std::unique_lock(submission_mutex_); }

private:
    struct LiveResourceInfo {
        const RHITrackedResource* resource = nullptr;
        RHIResourceKind kind = RHIResourceKind::Buffer;
        std::string name;
    };

    mutable std::mutex live_resources_mutex_;
    std::vector<LiveResourceInfo> live_resources_;

    uint64_t device_generation_ = 0;
    std::atomic<uint64_t> next_submission_value_ = 0;
    std::atomic<uint64_t> last_submission_value_ = 0;
    std::unique_ptr<Fence> submission_fence_;
    std::mutex submission_mutex_;

    struct DeferredReleaseBatch {
        SubmissionToken token;
        std::vector<DeferredRelease> releases;
    };
    mutable std::mutex deferred_release_mutex_;
    std::deque<DeferredReleaseBatch> deferred_release_batches_;
};

// ============================================================
}  // namespace mulan::engine
