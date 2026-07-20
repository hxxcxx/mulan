#include "engine_settings.h"
#include <mulan/core/log/log.h>
#include <mulan/rhi/device_factory.h>
#include <QColor>
using namespace mulan::engine;

namespace {

const char* backendName(GraphicsBackend backend) {
    switch (backend) {
    case GraphicsBackend::OpenGL: return "OpenGL";
    case GraphicsBackend::Vulkan: return "Vulkan";
    case GraphicsBackend::D3D11: return "D3D11";
    case GraphicsBackend::D3D12: return "D3D12";
    }
    return "Unknown";
}

}  // namespace

EngineSettings& EngineSettings::instance() {
    static EngineSettings s;
    return s;
}

EngineSettings::EngineSettings() {
    load();
}

// --- 后端 ---

GraphicsBackend EngineSettings::backend() const {
    std::scoped_lock lock(mutex_);
    const auto& factory = DeviceFactory::instance();
    if (!factory.find(backend_) && !factory.modules().empty())
        return factory.modules().front().backend;
    return backend_;
}

void EngineSettings::setBackend(GraphicsBackend b) {
    std::scoped_lock lock(mutex_);
    if (backend_ != b) {
        LOG_INFO("[AppConfig] Rendering backend changed: {} -> {}", backendName(backend_), backendName(b));
        backend_ = b;
        saveLocked();
    }
}

// --- 抗锯齿 ---

MSAALevel EngineSettings::msaa() const {
    std::scoped_lock lock(mutex_);
    return msaa_;
}

void EngineSettings::setMsaa(MSAALevel level) {
    std::scoped_lock lock(mutex_);
    if (msaa_ != level) {
        msaa_ = level;
        saveLocked();
    }
}

// --- VSync ---

bool EngineSettings::vsync() const {
    std::scoped_lock lock(mutex_);
    return vsync_;
}

void EngineSettings::setVsync(bool v) {
    std::scoped_lock lock(mutex_);
    if (vsync_ != v) {
        vsync_ = v;
        saveLocked();
    }
}

// --- 背景色 ---

QColor EngineSettings::backgroundColor() const {
    std::scoped_lock lock(mutex_);
    return bgcolor_;
}
void EngineSettings::setBackgroundColor(const QColor& color) {
    std::scoped_lock lock(mutex_);
    if (bgcolor_ != color) {
        bgcolor_ = color;
        saveLocked();
    }
}

// --- IBL 开关 / HDR 路径 ---

bool EngineSettings::iblEnabled() const {
    std::scoped_lock lock(mutex_);
    return ibl_enabled_;
}
void EngineSettings::setIblEnabled(bool enabled) {
    std::scoped_lock lock(mutex_);
    if (ibl_enabled_ != enabled) {
        ibl_enabled_ = enabled;
        saveLocked();
    }
}

QString EngineSettings::hdrPath() const {
    std::scoped_lock lock(mutex_);
    return hdr_path_;
}
void EngineSettings::setHdrPath(const QString& path) {
    std::scoped_lock lock(mutex_);
    if (hdr_path_ != path) {
        hdr_path_ = path;
        saveLocked();
    }
}

// --- 批量应用 / 读取 ---

void EngineSettings::applyTo(mulan::view::ViewConfig& cfg) const {
    std::scoped_lock lock(mutex_);
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
    std::scoped_lock lock(mutex_);
    backend_ = cfg.backend;
    msaa_ = cfg.msaa;
    vsync_ = cfg.vsync;
    bgcolor_ = QColor::fromRgbF(cfg.clearColor[0], cfg.clearColor[1], cfg.clearColor[2], cfg.clearColor[3]);
    ibl_enabled_ = cfg.iblEnabled;
    hdr_path_ = QString::fromStdString(cfg.hdrPath);
    saveLocked();
}

// --- 持久化 ---

static int backendToInt(GraphicsBackend b) {
    return static_cast<int>(b);
}
static GraphicsBackend intToBackend(int v) {
    switch (static_cast<GraphicsBackend>(v)) {
    case GraphicsBackend::OpenGL: return GraphicsBackend::OpenGL;
    case GraphicsBackend::Vulkan: return GraphicsBackend::Vulkan;
    case GraphicsBackend::D3D12: return GraphicsBackend::D3D12;
    case GraphicsBackend::D3D11: return GraphicsBackend::D3D11;
    default: return GraphicsBackend::Vulkan;
    }
}

static int msaaToInt(MSAALevel l) {
    return static_cast<int>(l);
}
static MSAALevel intToMsaa(int v) {
    switch (v) {
    case 1: return MSAALevel::None;
    case 2: return MSAALevel::x2;
    case 4: return MSAALevel::x4;
    case 8: return MSAALevel::x8;
    default: return MSAALevel::x4;
    }
}

void EngineSettings::saveLocked() {
    qsettings_.setValue("backend", backendToInt(backend_));
    qsettings_.setValue("msaa", msaaToInt(msaa_));
    qsettings_.setValue("vsync", vsync_);
    qsettings_.setValue("backgroundColor", bgcolor_);
    qsettings_.setValue("iblEnabled", ibl_enabled_);
    qsettings_.setValue("hdrPath", hdr_path_);
    LOG_DEBUG("[AppConfig] Settings saved: backend={}, msaa={}, vsync={}, ibl={}", backendName(backend_),
              msaaToInt(msaa_), vsync_, ibl_enabled_);
}

void EngineSettings::load() {
    backend_ = intToBackend(qsettings_.value("backend", backendToInt(GraphicsBackend::Vulkan)).toInt());
    msaa_ = intToMsaa(qsettings_.value("msaa", 4).toInt());
    vsync_ = qsettings_.value("vsync", true).toBool();
    const QColor defaultBackground = QColor::fromRgb(97, 101, 118);
    bgcolor_ = qsettings_.value("backgroundColor", defaultBackground).value<QColor>();
    ibl_enabled_ = qsettings_.value("iblEnabled", false).toBool();
    hdr_path_ = qsettings_.value("hdrPath", QStringLiteral("assets/envmap.hdr")).toString();
    LOG_INFO("[AppConfig] Settings loaded: backend={}, msaa={}, vsync={}, ibl={}, hdr={}", backendName(backend_),
             msaaToInt(msaa_), vsync_, ibl_enabled_, hdr_path_.toStdString());
}
