/**
 * @file EngineSettings.h
 * @brief 引擎渲染设置单例 — 持久化 ViewConfig 中 UI 可控的部分
 * @author hxxcxx
 * @date 2026-04-24
 */
#pragma once

#include <mulan/engine/Window.h>
#include <mulan/world/ViewConfig.h>
#include <QColor>
#include <QSettings>

namespace mulan::engine {
struct ViewConfig;
}

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

    /// 填充 engine::ViewConfig
    void applyTo(mulan::engine::ViewConfig& cfg) const;

    /// 填充 world::ViewConfig（新增）
    void applyTo(mulan::world::ViewConfig& cfg) const;

    /// 从 ViewConfig 读取（用于首次保存默认值以外的场景）
    void loadFrom(const mulan::engine::ViewConfig& cfg);

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

    QSettings m_qsettings{"MulanGeo", "Engine"};

    mulan::engine::GraphicsBackend           m_backend = mulan::engine::GraphicsBackend::Vulkan;
    mulan::engine::RenderConfig::MSAALevel   m_msaa    = mulan::engine::RenderConfig::MSAALevel::x4;
    bool                                        m_vsync   = true;
    QColor                                     m_bgcolor;
};
