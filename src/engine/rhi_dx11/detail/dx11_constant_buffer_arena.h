#pragma once

#include "dx11_common.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

class DX11ConstantBufferArena final {
public:
    struct Allocation {
        ID3D11Buffer* buffer = nullptr;
        UINT firstConstant = 0;
        UINT constantCount = 0;
        bool ranged = false;

        explicit operator bool() const { return buffer != nullptr; }
    };

    DX11ConstantBufferArena(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DeviceContext1* context1);

    void beginFrame();
    Allocation upload(const void* data, uint32_t size);

    bool isValid() const { return device_ && context_; }
    bool usesLinearSuballocation() const { return linear_suballocation_; }

private:
    static constexpr uint32_t kPageBytes = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
    static constexpr uint32_t kAllocationAlignment = 256;

    struct Page {
        ComPtr<ID3D11Buffer> buffer;
        uint32_t cursor = 0;
        bool discarded = false;
    };

    bool createPage(Page& page);
    Page* acquirePage(uint32_t bytes);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11DeviceContext1* context1_ = nullptr;
    bool linear_suballocation_ = false;
    size_t active_page_ = 0;
    std::vector<Page> pages_;
};

}  // namespace mulan::engine
