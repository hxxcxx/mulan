/**
 * @file dx11_buffer.h
 * @brief D3D11 缓冲区实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/buffer.h"
#include "dx11_common.h"

#include <vector>

namespace mulan::engine {

class DX11Buffer final : public Buffer {
public:
    DX11Buffer(const BufferDesc& desc, ID3D11Device* device, ID3D11DeviceContext* ctx);
    ~DX11Buffer() = default;

    const BufferDesc& desc() const override { return m_desc; }
    void update(uint32_t offset, uint32_t size, const void* data) override;
    bool readback(uint32_t offset, uint32_t size, void* outData) override;

    ID3D11Buffer* buffer() const { return m_buffer.Get(); }
    uint32_t allocationSize() const { return m_byteWidth; }

private:
    BufferDesc m_desc;
    ComPtr<ID3D11Buffer> m_buffer;
    ID3D11DeviceContext* m_ctx;  // immediate context, not owned
    uint32_t m_byteWidth = 0;
    D3D11_USAGE m_nativeUsage = D3D11_USAGE_DEFAULT;
    std::vector<uint8_t> m_dynamicShadow;
};

}  // namespace mulan::engine
