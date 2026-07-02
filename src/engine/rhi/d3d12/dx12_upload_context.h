/**
 * @file dx12_upload_context.h
 * @brief D3D12 上传管理器，Staging Buffer 池与 GPU 传输
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "dx12_common.h"

#include "../texture.h"

#include <vector>

namespace mulan::engine {

class DX12Buffer;
class DX12Texture;

class DX12UploadContext {
public:
    DX12UploadContext(ID3D12Device* device,
                      ID3D12CommandQueue* queue,
                      uint32_t frameCount);
    ~DX12UploadContext();

    /// 上传 Immutable/Default buffer 的初始数据
    void uploadBuffer(DX12Buffer* dst, const void* data, uint32_t size,
                      uint32_t dstOffset = 0);

    /// 上传像素数据到纹理：staging 拷贝 + 资源状态转到 PIXEL_SHADER_RESOURCE。
    /// 同步等待 GPU 完成。仅支持单 mip、非压缩颜色格式。
    void uploadTexture(DX12Texture* dst, const void* data, uint32_t width, uint32_t height,
                       TextureFormat format);

    /// 提交并等待所有上传命令完成
    void flush();

private:
    ID3D12Device*         device_;
    ID3D12CommandQueue*   queue_;
    ComPtr<ID3D12CommandAllocator> cmd_allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    ComPtr<ID3D12Fence>   fence_;
    HANDLE                fence_event_ = nullptr;
    uint64_t              fence_value_ = 0;
    bool                  has_commands_ = false;

    static constexpr uint32_t kStagingSize = 4 * 1024 * 1024;  // 4MB per slab

    struct StagingSlab {
        ComPtr<ID3D12Resource> resource;
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t used = 0;
    };

    std::vector<StagingSlab> slabs_;
    StagingSlab& getOrCreateSlab(uint32_t minSize);
};

} // namespace mulan::engine
