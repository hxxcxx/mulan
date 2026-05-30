/**
 * @file DX11Fence.cpp
 * @brief D3D11 Fence 实现
 * @author zmb
 * @date 2026-04-19
 *
 * D3D11 没有原生 Fence 对象。使用 ID3D11Query(EVENT) 模拟。
 * signal() 记录值，wait() 通过 query spin-wait 等待 GPU 完成。
 */
#include "DX11Fence.h"

namespace mulan::engine
{

DX11Fence::DX11Fence(ID3D11Device* device, uint64_t initialValue)
    : m_signaled(initialValue)
    , m_completed(initialValue)
{
    D3D11_QUERY_DESC qd = {};
    qd.Query = D3D11_QUERY_EVENT;
    HRESULT hr = device->CreateQuery(&qd, &m_query);
    DX11_CHECK(hr);

    // Get immediate context
    ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(&ctx);
    m_ctx = ctx.Detach();  // we store raw ptr; device outlives fence
}

void DX11Fence::signal(uint64_t value)
{
    m_signaled = value;
    if (m_ctx && m_query)
    {
        m_ctx->End(m_query.Get());
    }
}

void DX11Fence::wait(uint64_t value)
{
    if (m_completed >= value) return;
    if (!m_ctx || !m_query)
    {
        m_completed = value;
        return;
    }
    // Spin-wait for GPU to complete
    BOOL data = FALSE;
    while (m_ctx->GetData(m_query.Get(), &data, sizeof(data), 0) == S_FALSE)
    {
        // yield
    }
    m_completed = m_signaled;
}

uint64_t DX11Fence::completedValue() const
{
    return m_completed;
}

} // namespace mulan::engine
