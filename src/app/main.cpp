#include "bootstrap/rhi_backends.h"
#include "ui/main_window.h"
#include <mulan/core/log/log.h>
#include <mulan/modeling/runtime/runtime.h>
#include <mulan/rhi/device_factory.h>
#include <QApplication>
#include <QFile>
#include <QIcon>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    // HiDPI 支持
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#ifdef _WIN32
    // 确保调试输出可见（附加到父进程控制台或新建控制台）
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        freopen("CONOUT$", "w", stderr);
    }
#endif

    mulan::core::log::init();
    LOG_INFO("[App] Starting mulan");

    auto& deviceFactory = mulan::engine::DeviceFactory::instance();
    if (auto result = mulan::app::registerLinkedRHIBackends(deviceFactory); !result) {
        LOG_ERROR("[App] RHI backend registration failed: {}", result.error().message);
        mulan::core::log::shutdown();
        return 1;
    }

    // 初始化组装层：注册建模后端（STEP/IGES 读取等接入 modeling_core）。
    mulan::runtime::init();

    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
    app.setWindowIcon(QIcon(":/app/branding/app-icon.png"));
    QFile appStyleFile(":/app/style/app.qss");
    if (appStyleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(appStyleFile.readAll()));
        LOG_DEBUG("[App] Application stylesheet loaded");
    } else {
        LOG_WARN("[App] Application stylesheet could not be loaded");
    }

    int exitCode = 0;
    {
        MainWindow window;
        window.show();
        LOG_INFO("[App] Main window shown");
        exitCode = app.exec();
    }

    LOG_INFO("[App] Shutting down: exitCode={}", exitCode);
    mulan::core::log::shutdown();
    return exitCode;
}
