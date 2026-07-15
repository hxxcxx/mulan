#include "detail/dx11_fence.h"
#include "../rhi/engine_error_code.h"

#include <algorithm>
#include <cstdint>
#include <thread>

namespace mulan::engine {

DX11Fence::DX11Fence(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t initialValue)
    : m_device(device), m_ctx(context), m_signaled(initialValue), m_completed(initialValue) {
    if (!m_device || !m_ctx)
        LOG_ERROR("[DX11] Fence initialization rejected: invalid device or context");
}

ResultVoid DX11Fence::signal(uint64_t value) {
    if (value <= m_signaled) {
        if (value < m_signaled) {
            return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 fence values must be monotonic"));
        }
        return {};
    }

    D3D11_QUERY_DESC desc = {};
    desc.Query = D3D11_QUERY_EVENT;
    PendingSignal pending;
    pending.value = value;
    HRESULT hr = m_device->CreateQuery(&desc, &pending.query);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11Device::CreateQuery(fence event)");
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 fence event query creation failed"));
    }

    m_ctx->End(pending.query.Get());
    // Event query 必须进入驱动命令流，wait() 才能观察到完成状态。
    m_ctx->Flush();
    m_pendingSignals.push_back(std::move(pending));
    m_signaled = value;
    return {};
}

ResultVoid DX11Fence::wait(uint64_t value) {
    if (m_completed >= value)
        return {};
    if (value > m_signaled) {
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionWaitFailed, "DX11 fence value has not been signaled"));
    }
    poll(true, value);
    if (m_completed < value)
        return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, "DX11 fence wait failed"));
    return {};
}

uint64_t DX11Fence::completedValue() const {
    const_cast<DX11Fence*>(this)->poll(false, UINT64_MAX);
    return m_completed;
}

void DX11Fence::poll(bool waitForTarget, uint64_t targetValue) {
    while (!m_pendingSignals.empty()) {
        auto& pending = m_pendingSignals.front();
        BOOL completed = FALSE;
        HRESULT hr = S_FALSE;
        do {
            hr = m_ctx->GetData(pending.query.Get(), &completed, sizeof(completed),
                                waitForTarget ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE && waitForTarget)
                std::this_thread::yield();
        } while (hr == S_FALSE && waitForTarget);

        if (hr == S_FALSE || !completed)
            return;
        if (FAILED(hr)) {
            logDX11Failure(hr, "ID3D11DeviceContext::GetData(fence event)");
            return;
        }

        m_completed = (std::max) (m_completed, pending.value);
        m_pendingSignals.pop_front();
        if (m_completed >= targetValue)
            return;
    }
}

}  // namespace mulan::engine
