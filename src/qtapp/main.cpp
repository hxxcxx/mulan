#include "UI/MainWindow.h"
#include <QApplication>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    // HiDPI 支持
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#ifdef _WIN32
    // 确保调试输出可见（附加到父进程控制台或新建控制台）
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        freopen("CONOUT$", "w", stderr);
    }
#endif

    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);

    MainWindow window;
    window.show();

    return app.exec();
}
