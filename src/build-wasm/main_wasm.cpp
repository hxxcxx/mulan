/**
 * @file main_wasm.cpp
 * @brief WebAssembly / WebGL 入口点
 *
 * 使用 Emscripten 的 WebGL API 创建 WebGL2 上下文，
 * 绕过 GLAD（浏览器环境无需动态加载函数指针），
 * 直接初始化 MulanGeo Engine 的 OpenGL 渲染后端。
 *
 * 编译条件: MULAN_GEO_WASM 宏由 CMakeLists.txt 定义
 */

#ifdef MULAN_GEO_WASM

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <memory>

// Engine headers
#include "RHI/Device.h"
#include "RHI/SwapChain.h"
#include "RHI/CommandList.h"
#include "RHI/OpenGL/GLDevice.h"
#include "RHI/OpenGL/GLSwapChain.h"
#include "RHI/OpenGL/GLCommandList.h"
#include "Scene/Camera.h"
#include "Scene/Scene.h"
#include "Render/SceneRenderer.h"
#include "Window.h"

using namespace mulan::Engine;

// ── 全局状态 ────────────────────────────────────────────────

namespace {

struct WasmApp {
    std::shared_ptr<GLDevice>      device;
    std::unique_ptr<GLSwapChain>   swapChain;
    std::unique_ptr<GLCommandList>  cmdList;
    std::unique_ptr<SceneRenderer> renderer;

    int canvasWidth  = 800;
    int canvasHeight = 600;

    bool initialized = false;
};

WasmApp g_app;

// ── WebGL2 上下文创建（Emscripten API）──────────────────────

bool createWebGLContext(int width, int height) {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion      = 2;   // WebGL 2 → OpenGL ES 3
    attrs.minorVersion      = 0;
    attrs.depth             = true;
    attrs.stencil           = false;
    attrs.antialias         = false;
    attrs.alpha             = false;
    attrs.premultipliedAlpha = false;
    attrs.preserveDrawingBuffer = false;
    attrs.powerPreference   = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.enableExtensionsByDefault = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        emscripten_webgl_create_context("#canvas", &attrs);

    if (ctx <= 0) {
        std::fprintf(stderr, "[WASM] Failed to create WebGL2 context (code=%d)\n",
                     static_cast<int>(ctx));
        return false;
    }

    EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(ctx);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        std::fprintf(stderr, "[WASM] Failed to make WebGL2 context current (code=%d)\n",
                     static_cast<int>(res));
        return false;
    }

    std::fprintf(stdout, "[WASM] WebGL2 context created (%dx%d)\n", width, height);
    return true;
}

// ── 每帧回调 ─────────────────────────────────────────────────

void mainLoop() {
    if (!g_app.initialized) return;

    // 检测画布尺寸变化
    int w = 0, h = 0;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    if (w != g_app.canvasWidth || h != g_app.canvasHeight) {
        g_app.canvasWidth  = w;
        g_app.canvasHeight = h;
        if (g_app.swapChain) {
            g_app.swapChain->resize(static_cast<uint32_t>(w),
                                    static_cast<uint32_t>(h));
        }
    }

    // ── 渲染一帧 ──
    if (g_app.swapChain && g_app.cmdList) {
        g_app.cmdList->beginRenderPass(g_app.swapChain->renderPassBeginInfo());

        // TODO: 在此处通过 SceneRenderer 渲染场景
        // g_app.renderer->render(scene, camera, g_app.swapChain.get());

        g_app.cmdList->endRenderPass();
        g_app.swapChain->present();
    }
}

} // anonymous namespace

// ── main ─────────────────────────────────────────────────────

int main() {
    std::fprintf(stdout, "[WASM] MulanGeo WebAssembly starting...\n");

    g_app.canvasWidth  = 800;
    g_app.canvasHeight = 600;

    // 1. 设置画布尺寸
    emscripten_set_canvas_element_size("#canvas",
                                       g_app.canvasWidth,
                                       g_app.canvasHeight);

    // 2. 创建 WebGL2 上下文
    if (!createWebGLContext(g_app.canvasWidth, g_app.canvasHeight)) {
        std::fprintf(stderr, "[WASM] Initialization failed: WebGL2 not available\n");
        return 1;
    }

    // 3. 初始化 GLDevice
    //    Emscripten 环境下无 WGL 上下文，GLAD 由 Emscripten 的 GL 库替代。
    //    NativeWindowHandle 留空（WebGL 上下文已由 Emscripten 管理）。
    GLDevice::CreateInfo ci;
    ci.enableValidation = false;  // WebGL 不支持 GL_DEBUG_OUTPUT
    ci.window.type      = NativeWindowHandle::Type::Unknown; // Emscripten 管理上下文

    g_app.device = std::make_shared<GLDevice>(ci);
    if (!g_app.device->isInitialized()) {
        std::fprintf(stderr, "[WASM] GLDevice initialization failed\n");
        return 1;
    }

    // 4. 创建 GLSwapChain（映射到默认 Framebuffer / Canvas）
    SwapChainDesc scDesc;
    scDesc.width       = static_cast<uint32_t>(g_app.canvasWidth);
    scDesc.height      = static_cast<uint32_t>(g_app.canvasHeight);
    scDesc.bufferCount = 1;   // WebGL 单缓冲（浏览器自动管理）
    scDesc.vsync       = true;
    scDesc.format      = TextureFormat::RGBA8_UNorm;

    GLSwapChain::InitParams initParams; // Win32 字段在 WASM 下为空

    RenderConfig renderConfig;
    renderConfig.clearColor[0] = 0.12f;
    renderConfig.clearColor[1] = 0.13f;
    renderConfig.clearColor[2] = 0.18f;
    renderConfig.clearColor[3] = 1.0f;

    g_app.swapChain = std::make_unique<GLSwapChain>(scDesc, initParams, renderConfig);

    // 创建 GL 命令列表
    g_app.cmdList = std::make_unique<GLCommandList>();

    // 5. 创建场景渲染器
    g_app.renderer = std::make_unique<SceneRenderer>(g_app.device.get());

    g_app.initialized = true;
    std::fprintf(stdout, "[WASM] Initialization complete. Starting render loop.\n");

    // 6. 启动 Emscripten 渲染循环（0 = 浏览器 requestAnimationFrame 节奏）
    emscripten_set_main_loop(mainLoop, 0, true);

    return 0;
}

#endif // MULAN_GEO_WASM
