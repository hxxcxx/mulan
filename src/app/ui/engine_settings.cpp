#include "engine_settings.h"
#include <QColor>
using namespace mulan::engine;

EngineSettings& EngineSettings::instance() {
    static EngineSettings s;
    return s;
}

EngineSettings::EngineSettings() {
    load();
}

// --- 后端 ---

GraphicsBackend EngineSettings::backend() const {
    return backend_;
}

void EngineSettings::setBackend(GraphicsBackend b) {
    if (backend_ != b) {
        backend_ = b;
        save();
    }
}

// --- 抗锯齿 ---

RenderConfig::MSAALevel EngineSettings::msaa() const {
    return msaa_;
}

void EngineSettings::setMsaa(RenderConfig::MSAALevel level) {
    if (msaa_ != level) {
        msaa_ = level;
        save();
    }
}

// --- VSync ---

bool EngineSettings::vsync() const {
    return vsync_;
}

void EngineSettings::setVsync(bool v) {
    if (vsync_ != v) {
        vsync_ = v;
        save();
    }
}

// --- 背景色 ---

QColor EngineSettings::backgroundColor() const {
    return bgcolor_;
}
void EngineSettings::setBackgroundColor(const QColor& color) {
    if (bgcolor_ != color) {
        bgcolor_ = color;
        save();
    }
}

// --- IBL 开关 / HDR 路径 ---

bool EngineSettings::iblEnabled() const {
    return ibl_enabled_;
}
void EngineSettings::setIblEnabled(bool enabled) {
    if (ibl_enabled_ != enabled) {
        ibl_enabled_ = enabled;
        save();
    }
}

QString EngineSettings::hdrPath() const {
    return hdr_path_;
}
void EngineSettings::setHdrPath(const QString& path) {
    if (hdr_path_ != path) {
        hdr_path_ = path;
        save();
    }
}

// --- 批量应用 / 读取 ---

void EngineSettings::applyTo(mulan::view::ViewConfig& cfg) const {
    cfg.backend = backend_;
    cfg.msaa = msaa_;
    cfg.vsync = vsync_;
    cfg.clearColor[0] = static_cast<float>(bgcolor_.redF());
    cfg.clearColor[1] = static_cast<float>(bgcolor_.greenF());
    cfg.clearColor[2] = static_cast<float>(bgcolor_.blueF());
    cfg.clearColor[3] = static_cast<float>(bgcolor_.alphaF());
    cfg.iblEnabled = ibl_enabled_;
    cfg.hdrPath = hdr_path_.toStdString();
}

void EngineSettings::loadFrom(const mulan::view::ViewConfig& cfg) {
    backend_ = cfg.backend;
    msaa_ = cfg.msaa;
    vsync_ = cfg.vsync;
    bgcolor_ = QColor::fromRgbF(cfg.clearColor[0], cfg.clearColor[1], cfg.clearColor[2], cfg.clearColor[3]);
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_ = QString::fromStdString(cfg.hdrPath);
    save();
}

// --- 持久化 ---

static int backendToInt(GraphicsBackend b) {
    return static_cast<int>(b);
}
static GraphicsBackend intToBackend(int v) {
    switch (static_cast<GraphicsBackend>(v)) {
    case GraphicsBackend::Vulkan: return GraphicsBackend::Vulkan;
    case GraphicsBackend::D3D12: return GraphicsBackend::D3D12;
    // OpenGL / D3D11 后端已移除，旧配置回退到 Vulkan
    case GraphicsBackend::OpenGL:
    case GraphicsBackend::D3D11:
    default: return GraphicsBackend::Vulkan;
    }
}

static int msaaToInt(RenderConfig::MSAALevel l) {
    return static_cast<int>(l);
}
static RenderConfig::MSAALevel intToMsaa(int v) {
    switch (v) {
    case 1: return RenderConfig::MSAALevel::None;
    case 2: return RenderConfig::MSAALevel::x2;
    case 4: return RenderConfig::MSAALevel::x4;
    case 8: return RenderConfig::MSAALevel::x8;
    default: return RenderConfig::MSAALevel::x4;
    }
}

void EngineSettings::save() {
    qsettings_.setValue("backend", backendToInt(backend_));
    qsettings_.setValue("msaa", msaaToInt(msaa_));
    qsettings_.setValue("vsync", vsync_);
    qsettings_.setValue("backgroundColor", bgcolor_);
    qsettings_.setValue("iblEnabled", ibl_enabled_);
    qsettings_.setValue("hdrPath", hdr_path_);
}

void EngineSettings::load() {
    backend_ = intToBackend(qsettings_.value("backend", backendToInt(GraphicsBackend::Vulkan)).toInt());
    msaa_ = intToMsaa(qsettings_.value("msaa", 4).toInt());
    vsync_ = qsettings_.value("vsync", true).toBool();
    const QColor defaultBackground = QColor::fromRgb(63, 63, 63);
    const QColor previousDefaultBackground = QColor::fromRgb(25, 36, 48);
    const QColor legacyDefaultBackground = QColor::fromRgb(97, 101, 118);
    bgcolor_ = qsettings_.value("backgroundColor", defaultBackground).value<QColor>();
    // 将旧版默认值迁移到新默认值，同时保留用户主动选择的其他背景色。
    if (bgcolor_ == previousDefaultBackground || bgcolor_ == legacyDefaultBackground) {
        bgcolor_ = defaultBackground;
        qsettings_.setValue("backgroundColor", bgcolor_);
    }
    ibl_enabled_ = qsettings_.value("iblEnabled", false).toBool();
    hdr_path_ = qsettings_.value("hdrPath", QStringLiteral("assets/envmap.hdr")).toString();
}
