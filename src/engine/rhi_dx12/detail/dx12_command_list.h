/**
 * @file dx12_command_list.h
 * @brief D3D12 命令列表实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/command_list.h"
#include "dx12_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class DX12Texture;  // forward declaration

class DX12CommandList final : public CommandList {
public:
    /// 创建独立的 CommandList（拥有自己的 cmd allocator 和 cmd list）。
    /// 失败返回 CommandListCreateFailed。
    static core::Result<std::unique_ptr<DX12CommandList>> create(ID3D12Device* device,
                                                                 ID3D12CommandAllocator* allocator);
    /// 包装已有的 cmd list（帧循环用，不拥有）
    DX12CommandList(ID3D12GraphicsCommandList* existingCmdList);
    ~DX12CommandList();

    void begin() override;
    void end() override;

    void setPipelineState(PipelineState* pso) override;
    void setViewport(const Viewport& vp) override;
    void setScissorRect(const ScissorRect& rect) override;

    void bindGroup(BindGroup& group) override;
    void bindResources(const BindGroupDesc& desc) override;

    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) override;
    void setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0, IndexType type = IndexType::UInt32) override;

    void draw(const DrawAttribs& attribs) override;
    void drawIndexed(const DrawIndexedAttribs& attribs) override;
    void drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount = 1, uint32_t stride = 0) override;

    // --- Compute ---
    void dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) override;
    void dispatchIndirect(Buffer* argsBuffer, uint32_t offset) override;

    // --- Push Constants ---
    void setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) override;

    void updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                      ResourceTransitionMode mode = ResourceTransitionMode::Transition) override;

    void transitionResource(Buffer* buffer, ResourceState newState) override;
    void transitionResource(Texture* texture, ResourceState newState) override;
    void copyTextureToBuffer(Texture* src, Buffer* dst) override;

    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;
    void clearStencil(uint8_t stencil) override;

    // --- RenderPass ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;

    ID3D12GraphicsCommandList* commandList() const { return cmd_list_.Get(); }

    /// 设置内部命令列表（帧循环中使用外部 cmd list）
    void setCommandList(ID3D12GraphicsCommandList* cmdList);

    /// 设置间接绘制 CommandSignature（由 DX12Device 注入）
    void setIndirectSignatures(ID3D12CommandSignature* drawSig, ID3D12CommandSignature* dispatchSig) {
        draw_indirect_sig_ = drawSig;
        dispatch_indirect_sig_ = dispatchSig;
    }

    /// 设置当前帧的 CBV/SRV/UAV heap 和 sampler heap。
    /// 两个 heap 必须在同一次 SetDescriptorHeaps 调用中绑定，
    /// 否则后绑定的 heap 会覆盖前一个 heap。
    void setDescriptorHeap(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpuBase,
                           D3D12_GPU_DESCRIPTOR_HANDLE gpuBase, uint32_t descriptorSize,
                           ID3D12DescriptorHeap* samplerHeap = nullptr, uint32_t descriptorCapacity = 0);

    // 注入当前帧的 frame token（由 DX12Device::frameCommandList 设置）。
    // bindGroup 据此判断 BindGroup 缓存的 GPU descriptor handle 是否跨帧失效。
    void setFrameToken(uint64_t token) { frame_token_ = token; }

private:
    /// 独立模式私有构造（create() 使用）
    DX12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator);
    void bindDescriptorHeaps();

    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    bool owns_cmd_list_ = true;            // 是否在析构时释放
    bool recording_ = false;
    uint32_t cached_stride_ = 0;           // 从 PSO vertexLayout 缓存的 stride
    bool rp_present_source_ = false;       // endRenderPass 中决定 barrier 目标状态（PRESENT vs SRV）
    DX12Texture* rp_color_tex_ = nullptr;  // 当前 render pass 的颜色附件
    DX12Texture* rp_resolve_tex_ = nullptr;

    // 纹理绑定用：当前帧的描述符堆
    ID3D12DescriptorHeap* desc_heap_ = nullptr;
    ID3D12DescriptorHeap* sampler_heap_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE desc_cpu_base_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE desc_gpu_base_ = {};
    uint32_t desc_size_ = 0;
    uint32_t desc_capacity_ = 1024;
    uint32_t desc_alloc_count_ = 0;  // 当前帧已分配数

    uint64_t frame_token_ = 0;       // 当前帧 token，0=独立/未注入

    ID3D12CommandSignature* draw_indirect_sig_ = nullptr;
    ID3D12CommandSignature* dispatch_indirect_sig_ = nullptr;
};

}  // namespace mulan::engine
