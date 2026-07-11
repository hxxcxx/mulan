/**
 * @file dx11_render_target.h
 * @brief D3D11 离屏渲染目标实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/render_target.h"
#include "dx11_common.h"
#include "dx11_texture.h"

#include <memory>

namespace mulan::engine {

class DX11RenderTarget final : public RenderTarget {
public:
    DX11RenderTarget(const RenderTargetDesc& desc, ID3D11Device* device);
    ~DX11RenderTarget() = default;

    const RenderTargetDesc& desc() const override { return m_desc; }
    Texture* colorTexture() override { return m_colorTexture.get(); }
    Texture* depthTexture() override { return m_depthTexture.get(); }

    void resize(uint32_t width, uint32_t height) override;

private:
    void createResources();

    RenderTargetDesc m_desc;
    ID3D11Device* m_device;
    std::unique_ptr<DX11Texture> m_colorTexture;
    std::unique_ptr<DX11Texture> m_depthTexture;
};

}  // namespace mulan::engine
