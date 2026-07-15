/**
 * @file dx11_buffer.h
 * @brief D3D11 缓冲区实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/buffer.h"
#include "dx11_common.h"

#include <memory>
#include <vector>

namespace mulan::engine {

class DX11Buffer final : public Buffer {
public:
    DX11Buffer(const BufferDesc& desc, ID3D11Device* device, ID3D11DeviceContext* ctx);
    ~DX11Buffer();

    const BufferDesc& desc() const override { return m_desc; }
    core::Result<void> write(uint32_t offset, uint32_t size, const void* data) override;
    core::Result<void> readback(uint32_t offset, uint32_t size, void* outData) override;

    ID3D11Buffer* buffer() const { return m_buffer.Get(); }
    const void* uniformData(uint32_t offset, uint32_t size) const;
    uint64_t uniformVersion() const { return m_uniformVersion; }
    bool isValid() const { return m_buffer != nullptr; }
    uint32_t allocationSize() const { return m_byteWidth; }
    bool isTransientUniformPage() const { return m_transientUniformPage; }

    static std::unique_ptr<DX11Buffer> createTransientUniformPage(uint32_t size, ID3D11Device* device,
                                                                  ID3D11DeviceContext* ctx);

private:
    DX11Buffer(uint32_t transientUniformPageSize, ID3D11Device* device, ID3D11DeviceContext* ctx);

    BufferDesc m_desc;
    ComPtr<ID3D11Buffer> m_buffer;
    ID3D11DeviceContext* m_ctx;  // 非拥有，设备保证其生命周期
    uint32_t m_byteWidth = 0;
    D3D11_USAGE m_nativeUsage = D3D11_USAGE_DEFAULT;
    std::vector<uint8_t> m_dynamicShadow;
    std::vector<uint8_t> m_uniformShadow;
    uint64_t m_uniformVersion = 1;
    bool m_transientUniformPage = false;
};

}  // namespace mulan::engine
