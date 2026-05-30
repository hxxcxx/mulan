/**
 * @file DX11Common.h
 * @brief D3D11 后端公共头文件，统一 include 与工具类型
 * @author zmb
 * @date 2026-04-19
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wrl/client.h>


#include <cstdio>
#include <cstdint>
#include <cassert>

#ifdef _DEBUG
#define DX11_CHECK(hr)                                                      \
    do {                                                                    \
        if (FAILED(hr)) {                                                   \
            fprintf(stderr, "[DX11 ERROR] HRESULT=0x%08X at %s:%d\n",      \
                    (unsigned)(hr), __FILE__, __LINE__);                     \
        }                                                                   \
    } while (0)
#else
#define DX11_CHECK(hr) (void)(hr)
#endif

namespace mulan::engine
{

using Microsoft::WRL::ComPtr;

} // namespace mulan::engine
