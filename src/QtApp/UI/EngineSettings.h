/**
 * @file EngineSettings.h
 * @brief 引擎渲染设置单例 — 持久化 ViewConfig 中 UI 可控的部分
 * @author hxxcxx
 * @date 2026-04-24
 */
#pragma once

#include <MulanGeo/Engine/Render/EngineView.h>
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

    /// 填充 ViewConfig 中 UI 可控的字段（不影响原生窗口句柄等）
    void applyTo(mulan::engine::ViewConfig& cfg) const;

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
