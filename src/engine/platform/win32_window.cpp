#include "win32_window.h"

#ifdef _WIN32

namespace mulan::engine {

Win32Window::Win32Window(const Desc& desc) : width_(desc.width), height_(desc.height) {
    renderConfig = desc.renderConfig;
    registerClass();
    createWindow(desc);
}

Win32Window::~Win32Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

NativeWindowHandle Win32Window::nativeHandle() const {
    return NativeWindowHandle::makeWin32(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                                         reinterpret_cast<uintptr_t>(hwnd_));
}

void Win32Window::pollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Win32Window::registerClass() {
    static bool registered = false;
    if (registered)
        return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR) IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;

    RegisterClassExW(&wc);
    registered = true;
}

void Win32Window::createWindow(const Desc& desc) {
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rc = { 0, 0, (LONG) desc.width, (LONG) desc.height };
    AdjustWindowRect(&rc, style, FALSE);

    hwnd_ = CreateWindowExW(0, kClassName, desc.title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                            rc.bottom - rc.top, nullptr, nullptr, GetModuleHandleW(nullptr),
                            this);  // 传 this 给 WM_NCCREATE

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
}

LRESULT CALLBACK Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Win32Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        switch (msg) {
        case WM_SIZE:
            self->width_ = LOWORD(lParam);
            self->height_ = HIWORD(lParam);
            self->notifyResize(self->width_, self->height_);
            return 0;

        case WM_CLOSE: self->should_close_ = true; return 0;

        case WM_DESTROY: self->should_close_ = true; return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace mulan::engine

#endif  // _WIN32
