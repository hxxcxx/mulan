/**
 * @file engine_settings.h
 * @brief 引擎渲染设置单例 — 持久化 ViewConfig 中 UI 可控的部分
 * @author hxxcxx
 * @date 2026-04-24
 */
#pragma once

#include <mulan/rhi/window.h>
#include <mulan/view/core/view_config.h>
#include <QColor>
#include <QSettings>
#include <QString>

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

    /// 填充 ViewConfig
    void applyTo(mulan::view::ViewConfig& cfg) const;

    /// 从 ViewConfig 读取
    void loadFrom(const mulan::view::ViewConfig& cfg);

    /// 获取当前的背景色
    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);

    /// IBL（Image-Based Lighting）开关。默认关闭——需要把一张 HDR 放到 hdrPath()
    /// 指向的位置，开启后启动时一次性烘焙 irradiance/prefilter/BRDF LUT 三件套。
    /// 关闭时完全跳过烘焙，shader 走默认黑色 fallback，零开销。
    bool iblEnabled() const;
    void setIblEnabled(bool enabled);

    /// HDR 文件路径。默认 "assets/envmap.hdr"（相对进程工作目录）。
    QString hdrPath() const;
    void setHdrPath(const QString& path);

private:
    EngineSettings();
    ~EngineSettings() = default;
    EngineSettings(const EngineSettings&) = delete;
    EngineSettings& operator=(const EngineSettings&) = delete;

    void save();
    void load();

    QSettings qsettings_{ "mulan", "Engine" };

    mulan::engine::GraphicsBackend backend_ = mulan::engine::GraphicsBackend::Vulkan;
    mulan::engine::RenderConfig::MSAALevel msaa_ = mulan::engine::RenderConfig::MSAALevel::x4;
    bool vsync_ = true;
    QColor bgcolor_;
    bool ibl_enabled_ = false;  // 默认关
    QString hdr_path_{ "assets/envmap.hdr" };
};
