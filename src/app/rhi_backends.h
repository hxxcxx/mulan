/**
 * @file rhi_backends.h
 * @brief 应用实际链接的 RHI 后端组装入口
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

#include <mulan/core/result/error.h>

mulan::core::Result<void> registerApplicationRHIBackends();
