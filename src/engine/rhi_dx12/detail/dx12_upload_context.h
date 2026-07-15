/**
 * @file dx12_upload_context.h
 * @brief D3D12 上传管理器，Staging Buffer 池与 GPU 传输
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "dx12_common.h"

#include "../rhi/texture.h"

#include <vector>

namespace mulan::engine {

class DX12Buffer;
class DX12Texture;

class DX12UploadContext {
public:
    DX12UploadContext(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t frameCount);
    ~DX12UploadContext();

    /// 上传 Immutable/Default buffer 的初始数据
    ResultVoid uploadBuffer(DX12Buffer* dst, const void* data, uint32_t size, uint32_t dstOffset = 0);

    /// 上传像素数据到纹理：staging 拷贝 + 资源状态转到 PIXEL_SHADER_RESOURCE。
    /// 同步等待 GPU 完成。仅支持单 mip、非压缩颜色格式。
    ResultVoid uploadTexture(DX12Texture* dst, const TextureUploadDesc& upload);

    /// 开始批量上传：后续 uploadBuffer/uploadTexture 只录制到同一 cmd list，不提交。
    /// 配合 flushUploadBatch() 把多个上传合并成一次提交 + 一次 GPU 同步等待，
    /// 显著减少大模型加载（几十张纹理/网格）时的 GPU round-trip。
    ResultVoid beginUploadBatch();

    /// 结束批量上传：Close + Execute + Signal + Wait，一次提交本批次所有命令。
    ResultVoid flushUploadBatch();

    /// 提交并等待所有上传命令完成
    void flush();
    bool isValid() const { return device_ && queue_ && cmd_allocator_ && cmd_list_ && fence_ && fence_event_; }

private:
    /// 非批量模式下：录制完立即 Close→Execute→Signal→Wait（同步提交）。
    /// 批量模式下：不做任何提交，命令已录入 batch cmd list。
    ResultVoid submitIfNotBatching();

private:
    ID3D12Device* device_;
    ID3D12CommandQueue* queue_;
    ComPtr<ID3D12CommandAllocator> cmd_allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    uint64_t fence_value_ = 0;
    bool has_commands_ = false;
    bool batch_active_ = false;                                // 批量模式：cmd_list 保持 open，不逐次提交

    static constexpr uint32_t kStagingSize = 4 * 1024 * 1024;  // 4MB per slab

    struct StagingSlab {
        ComPtr<ID3D12Resource> resource;
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t used = 0;
    };

    std::vector<StagingSlab> slabs_;
    StagingSlab* getOrCreateSlab(uint64_t minSize, uint32_t alignment, uint32_t& alignedOffset);
};

}  // namespace mulan::engine
