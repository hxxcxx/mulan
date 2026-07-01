/**
 * @file input_event.h
 * @brief 平台无关的输入事件描述
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - InputEvent 是纯值类型，不依赖任何平台头文件
 *  - 由 Qt / Win32 / SDL 等平台层构造后传入 Operator
 *  - 鼠标坐标使用像素整数（左上角原点）
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

// ============================================================
// 鼠标按钮
// ============================================================

enum class MouseButton : uint8_t {
    None   = 0,
    Left   = 1,
    Right  = 2,
    Middle = 4,
};

inline constexpr MouseButton operator|(MouseButton a, MouseButton b) {
    return static_cast<MouseButton>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr bool operator&(MouseButton a, MouseButton b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================
// 修饰键
// ============================================================

enum class KeyModifier : uint8_t {
    None  = 0,
    Ctrl  = 1,
    Shift = 2,
    Alt   = 4,
};

inline constexpr KeyModifier operator|(KeyModifier a, KeyModifier b) {
    return static_cast<KeyModifier>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr bool operator&(KeyModifier a, KeyModifier b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================
// 键盘键码 — 只定义常用键，按需扩展
// ============================================================

enum class Key : uint16_t {
    Unknown = 0,
    Escape,  Enter,  Space,  Tab,  Backspace, Delete,
    Left, Right, Up, Down,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

// ============================================================
// 输入事件 — 统一的鼠标/键盘/滚轮事件
// ============================================================

struct InputEvent {
    enum class Type : uint8_t {
        MousePress,
        MouseRelease,
        MouseMove,
        MouseDoubleClick,
        Wheel,
        KeyPress,
        KeyRelease,
    };

    Type         type       = Type::MouseMove;
    MouseButton  button     = MouseButton::None;    ///< 当前按下/释放的按钮
    MouseButton  buttons    = MouseButton::None;    ///< 所有正在按住的按钮

    KeyModifier  modifiers  = KeyModifier::None;

    int          x          = 0;    ///< 像素坐标（左上角原点）
    int          y          = 0;

    // Wheel
    float        wheelDelta = 0.0f; ///< 正值向上/向前

    // Keyboard
    Key          key        = Key::Unknown;

    // --- 便捷查询 ---

    bool isMouseEvent() const {
        return type <= Type::MouseDoubleClick;
    }

    bool isKeyEvent() const {
        return type >= Type::KeyPress;
    }

    bool hasModifier(KeyModifier mod) const {
        return modifiers & mod;
    }

    bool isButtonPressed(MouseButton btn) const {
        return buttons & btn;
    }

    // --- 工厂 ---

    static InputEvent mousePress(int x, int y, MouseButton btn, MouseButton held, KeyModifier mods = KeyModifier::None) {
        return { Type::MousePress, btn, held | btn, mods, x, y, 0.0f, Key::Unknown };
    }

    static InputEvent mouseRelease(int x, int y, MouseButton btn, MouseButton held, KeyModifier mods = KeyModifier::None) {
        return { Type::MouseRelease, btn, held, mods, x, y, 0.0f, Key::Unknown };
    }

    static InputEvent mouseMove(int x, int y, MouseButton held, KeyModifier mods = KeyModifier::None) {
        return { Type::MouseMove, MouseButton::None, held, mods, x, y, 0.0f, Key::Unknown };
    }

    static InputEvent wheel(int x, int y, float delta, KeyModifier mods = KeyModifier::None) {
        return { Type::Wheel, MouseButton::None, MouseButton::None, mods, x, y, delta, Key::Unknown };
    }

    static InputEvent keyPress(Key k, KeyModifier mods = KeyModifier::None) {
        return { Type::KeyPress, MouseButton::None, MouseButton::None, mods, 0, 0, 0.0f, k };
    }

    static InputEvent keyRelease(Key k, KeyModifier mods = KeyModifier::None) {
        return { Type::KeyRelease, MouseButton::None, MouseButton::None, mods, 0, 0, 0.0f, k };
    }
};

} // namespace mulan::engine
