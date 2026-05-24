/**
 * @file Win32Window.h
 * @brief Win32 原生窗口，IWindow 的 Windows 平台实现
 * @author hxxcxx
 * @date 2026-04-16
 *
 * 直接创建 HWND，可脱离 Qt / SDL / GLFW 独立使用。
 * IWindow 只定义窗口属性（句柄、尺寸），事件循环由外部主循环负责。
 */

#pragma once

#include "../Window.h"

#ifdef _WIN32

#include <string>
#include <Windows.h>

namespace mulan::engine {

class Win32Window : public IWindow {
public:
    struct Desc {
        std::wstring title     = L"MulanGeo";
        uint32_t     width     = 1280;
        uint32_t     height    = 720;
        bool         resizable = true;
        RenderConfig renderConfig = {};
    };

    explicit Win32Window(const Desc& desc = {});
    ~Win32Window() override;

    // --- IWindow 接口 ---

    NativeWindowHandle nativeHandle() const override;
    uint32_t width()  const override { return m_width; }
    uint32_t height() const override { return m_height; }

    // --- Win32 特有（外部主循环使用，不是 IWindow 接口）---

    bool shouldClose() const { return m_shouldClose; }
    void pollEvents();
    void close() { m_shouldClose = true; }
    HWND hwnd() const { return m_hwnd; }

private:
    static constexpr const wchar_t* kClassName = L"MulanGeoWindow";

    void registerClass();
    void createWindow(const Desc& desc);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND     m_hwnd         = nullptr;
    uint32_t m_width        = 0;
    uint32_t m_height       = 0;
    bool     m_shouldClose  = false;
};

} // namespace mulan::Engine

#endif // _WIN32
