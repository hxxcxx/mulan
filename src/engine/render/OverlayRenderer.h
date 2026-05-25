/**
 * @file OverlayRenderer.h
 * @brief 叠加层渲染器 — 交互辅助图形（折线、橡皮线、控制点等）
 * @author hxxcxx
 * @date 2026-05-25
 */
#pragma once

namespace mulan::engine {

/// 叠加层渲染器 (空壳，待后续实现)
class OverlayRenderer {
public:
    OverlayRenderer() = default;
    ~OverlayRenderer() = default;

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;
};

} // namespace mulan::engine
