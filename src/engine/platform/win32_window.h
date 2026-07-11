/**
 * @file win32_window.h
 * @brief Win32 原生窗口，IWindow 的 Windows 平台实现
 * @author hxxcxx
 * @date 2026-04-16
 *
 * 直接创建 HWND，可脱离 Qt / SDL / GLFW 独立使用。
 * IWindow 只定义窗口属性（句柄、尺寸），事件循环由外部主循环负责。
 */

#pragma once

#include <mulan/rhi/window.h>

#ifdef _WIN32

#include <string>
#include <Windows.h>

namespace mulan::engine {

class Win32Window : public IWindow {
public:
    struct Desc {
        std::wstring title = L"mulan";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool resizable = true;
        RenderConfig renderConfig = {};
    };

    explicit Win32Window(const Desc& desc = {});
    ~Win32Window() override;

    // --- IWindow 接口 ---

    NativeWindowHandle nativeHandle() const override;
    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

    // --- Win32 特有（外部主循环使用，不是 IWindow 接口）---

    bool shouldClose() const { return should_close_; }
    void pollEvents();
    void close() { should_close_ = true; }
    HWND hwnd() const { return hwnd_; }

private:
    static constexpr const wchar_t* kClassName = L"mulanWindow";

    void registerClass();
    void createWindow(const Desc& desc);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool should_close_ = false;
};

}  // namespace mulan::engine

#endif  // _WIN32
