/**
 * @file backend.h
 * @brief D3D11 RHI 后端模块入口
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

namespace mulan::engine {

struct BackendModule;

const BackendModule& d3d11BackendModule();

}  // namespace mulan::engine
