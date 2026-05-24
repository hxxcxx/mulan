/**
 * @file DX11Fence.h
 * @brief D3D11 Fence 实现（使用 ID3D11Query 模拟）
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../Fence.h"
#include "DX11Common.h"

namespace MulanGeo::engine
{

class DX11Fence final : public Fence
{
public:
    DX11Fence(ID3D11Device* device, uint64_t initialValue);
    ~DX11Fence() = default;

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

private:
    ID3D11DeviceContext* m_ctx = nullptr;
    ComPtr<ID3D11Query>  m_query;
    uint64_t             m_signaled = 0;
    uint64_t             m_completed = 0;
};

} // namespace MulanGeo::Engine
