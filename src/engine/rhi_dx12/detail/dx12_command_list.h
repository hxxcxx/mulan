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
#include <array>
#include <memory>
#include <unordered_map>

namespace mulan::engine {

class DX12Texture;  // forward declaration
class DX12BindGroup;
class DX12TransientUniformArena;
class DX12DescriptorAllocator;

class DX12CommandList final : public CommandList {
public:
    /// 创建独立的 CommandList（拥有自己的 cmd allocator 和 cmd list）。
    /// 失败返回 CommandListCreateFailed。
    static Result<std::unique_ptr<DX12CommandList>> create(ID3D12Device* device, ID3D12CommandAllocator* allocator);
    /// 包装已有的 cmd list（帧循环用，不拥有）
    DX12CommandList(ID3D12GraphicsCommandList* existingCmdList);
    ~DX12CommandList();

    ResultVoid doBegin() override;
    ResultVoid doEnd() override;
    void doMarkSubmitted() override;

    void doSetPipelineState(PipelineState* pso) override;
    void doSetComputePipelineState(ComputePipelineState* pso) override;
    void doSetViewport(const Viewport& vp) override;
    void doSetScissorRect(const ScissorRect& rect) override;

    void doBindGroup(BindGroup& group) override;
    void doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) override;
    Result<UniformSlice> doWriteUniformBytes(std::span<const std::byte> data) override;

    void doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) override;
    void doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;
    void doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) override;

    void doDraw(const DrawAttribs& attribs) override;
    void doDrawIndexed(const DrawIndexedAttribs& attribs) override;
    void doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) override;

    // --- Compute ---
    void doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) override;
    void doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) override;

    // --- Push Constants ---
    void doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) override;

    void doTransitionResource(Texture* texture, ResourceState newState) override;
    ResultVoid doCopyTextureToBuffer(Texture* src, Buffer* dst) override;

    // --- RenderPass ---
    ResultVoid doBeginRenderPass(const RenderPassBeginInfo& info) override;
    void doEndRenderPass() override;

    ID3D12GraphicsCommandList* commandList() const { return cmd_list_.Get(); }
    ID3D12DescriptorHeap* descriptorHeap() const { return desc_heap_; }

    /// 设置内部命令列表（帧循环中使用外部 cmd list）
    void setCommandList(ID3D12GraphicsCommandList* cmdList);

    /// 设置间接绘制 CommandSignature（由 DX12Device 注入）。
    void setDrawIndirectSignature(ID3D12CommandSignature* signature) { draw_indirect_sig_ = signature; }

    /// 设置当前帧的 CBV/SRV/UAV heap 和 sampler heap。
    /// 两个 heap 必须在同一次 SetDescriptorHeaps 调用中绑定，
    /// 否则后绑定的 heap 会覆盖前一个 heap。
    void setDescriptorHeap(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpuBase,
                           D3D12_GPU_DESCRIPTOR_HANDLE gpuBase, uint32_t descriptorSize,
                           ID3D12DescriptorHeap* samplerHeap = nullptr, uint32_t descriptorCapacity = 0);

    /// 为独立命令列表安装私有描述符 arena，生命周期与命令列表一致。
    void setOwnedDescriptorArena(std::unique_ptr<DX12DescriptorAllocator> arena,
                                 ID3D12DescriptorHeap* samplerHeap = nullptr);

    // 注入当前帧的 frame token（由 DX12Device::frameCommandList 设置）。
    // bindGroup 据此判断 BindGroup 缓存的 GPU descriptor handle 是否跨帧失效。
    void setFrameToken(uint64_t token) { frame_token_ = token; }
    void setTransientUniformArena(DX12TransientUniformArena* arena) { transient_uniform_arena_ = arena; }

private:
    /// 独立模式私有构造（create() 使用）
    DX12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator);
    void bindDescriptorHeaps();
    void bindStaticGroup(DX12BindGroup& group);
    D3D12_RESOURCE_STATES textureState(DX12Texture* texture) const;
    void setTextureState(DX12Texture* texture, D3D12_RESOURCE_STATES state);

    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    bool owns_cmd_list_ = true;   // 是否在析构时释放
    bool recording_ = false;
    uint32_t cached_stride_ = 0;  // 从 PSO vertexLayout 缓存的 stride
    bool rp_present_source_ = false;
    uint8_t rp_color_count_ = 0;
    std::array<DX12Texture*, RenderPassBeginInfo::kMaxColorTargets> rp_color_textures_{};
    std::array<DX12Texture*, RenderPassBeginInfo::kMaxColorTargets> rp_resolve_textures_{};
    std::unordered_map<DX12Texture*, D3D12_RESOURCE_STATES> pending_texture_states_;

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
    std::unique_ptr<DX12TransientUniformArena> owned_transient_uniform_arena_;
    DX12TransientUniformArena* transient_uniform_arena_ = nullptr;
    std::unique_ptr<DX12DescriptorAllocator> owned_descriptor_arena_;
};

}  // namespace mulan::engine
