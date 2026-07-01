/**
 * @file engine_settings.h
 * @brief 引擎渲染设置单例 — 持久化 ViewConfig 中 UI 可控的部分
 * @author hxxcxx
 * @date 2026-04-24
 */
#pragma once

#include <mulan/engine/window.h>
#include <mulan/world/view_config.h>
#include <QColor>
#include <QSettings>

class EngineSettings {
public:
    static EngineSettings& instance();

    /// 后端
    mulan::engine::GraphicsBackend backend() const;
    void setBackend(mulan::engine::GraphicsBackend b);

    /// 抗锯齿
    mulan::engine::RenderConfig::MSAALevel msaa() const;
    void setMsaa(mulan::engine::RenderConfig::MSAALevel level);

    /// VSync
    bool vsync() const;
    void setVsync(bool v);

    /// 填充 world::ViewConfig
    void applyTo(mulan::world::ViewConfig& cfg) const;

    /// 从 ViewConfig 读取
    void loadFrom(const mulan::world::ViewConfig& cfg);

    /// 获取当前的背景色
    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);

private:
    EngineSettings();
    ~EngineSettings() = default;
    EngineSettings(const EngineSettings&) = delete;
    EngineSettings& operator=(const EngineSettings&) = delete;

    void save();
    void load();

    QSettings qsettings_{"mulan", "Engine"};

    mulan::engine::GraphicsBackend           backend_ = mulan::engine::GraphicsBackend::Vulkan;
    mulan::engine::RenderConfig::MSAALevel   msaa_    = mulan::engine::RenderConfig::MSAALevel::x4;
    bool                                        vsync_   = true;
    QColor                                     bgcolor_;
};
