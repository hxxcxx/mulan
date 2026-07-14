/**
 * @file backend.h
 * @brief OpenGL RHI 后端模块入口
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

namespace mulan::engine {

struct BackendModule;

const BackendModule& openGLBackendModule();

}  // namespace mulan::engine
