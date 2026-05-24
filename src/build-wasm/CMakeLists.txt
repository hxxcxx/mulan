cmake_minimum_required(VERSION 3.16)
project(MulanGeoWasm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Emscripten 检查 ──
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This CMakeLists.txt must be built with Emscripten (emcmake).")
endif()

# ── Engine 头文件路径 ──
set(ENGINE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Engine")

# ── 收集 Engine 中与 WASM 兼容的源文件（仅 OpenGL 后端 + 场景/渲染层）──
set(ENGINE_WASM_SOURCES
    # Scene
    ${ENGINE_SRC_DIR}/scene/SceneNode.cpp
    ${ENGINE_SRC_DIR}/scene/Scene.cpp
    ${ENGINE_SRC_DIR}/scene/Frustum.cpp
    ${ENGINE_SRC_DIR}/scene/Camera.cpp
    # Render
    ${ENGINE_SRC_DIR}/render/GeometryCache.cpp
    ${ENGINE_SRC_DIR}/render/SceneRenderer.cpp
    ${ENGINE_SRC_DIR}/render/EngineView.cpp
    # RHI base
    ${ENGINE_SRC_DIR}/rhi/DeviceFactory.cpp
    # vertex_layout_test.cpp 使用安装路径 include，不包含在 WASM 构建中
    # OpenGL backend
    ${ENGINE_SRC_DIR}/rhi/opengl/GLDevice.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLShader.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLSwapChain.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLPipelineState.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLBuffer.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLCommandList.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLRenderTarget.cpp
    ${ENGINE_SRC_DIR}/rhi/opengl/GLTexture.cpp
    # Geometry
    # (Mesh is header-only, no .cpp needed)
    # WASM entry point
    ${CMAKE_CURRENT_SOURCE_DIR}/main_wasm.cpp
)

add_executable(MulanGeoWasm ${ENGINE_WASM_SOURCES})

# ── 头文件目录 ──
target_include_directories(MulanGeoWasm PRIVATE
    ${ENGINE_SRC_DIR}
    ${ENGINE_SRC_DIR}/..        # src/ — 使 #include "Engine/..." 生效
)

# ── GLM ──
# Emscripten 构建时从源码引入 glm（header-only）
# 优先尝试 find_package，否则回退到 ${GLM_DIR}
find_package(glm CONFIG QUIET)
if(glm_FOUND)
    target_link_libraries(MulanGeoWasm PRIVATE glm::glm)
else()
    if(NOT GLM_DIR)
        # 尝试常见的相对位置
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/wasm32-emscripten/include")
            set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../vcpkg_installed/wasm32-emscripten/include")
        endif()
    endif()
    if(GLM_DIR)
        target_include_directories(MulanGeoWasm PRIVATE ${GLM_DIR})
    else()
        message(WARNING "GLM not found. Set GLM_DIR or install via vcpkg for wasm32-emscripten.")
    endif()
endif()

# ── 宏定义 ──
target_compile_definitions(MulanGeoWasm PRIVATE
    MULAN_GEO_WASM          # 供源码条件编译使用
    _USE_MATH_DEFINES
)

# ── Emscripten 链接选项 ──
set(EMSCRIPTEN_LINK_FLAGS
    "-sUSE_WEBGL2=1"
    "-sFULL_ES3=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sEXPORTED_FUNCTIONS=['_main']"
    "--bind"
)

# 如果有自定义 shell HTML 模板则使用（必须拆成两个独立参数）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "--shell-file"
        "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    )
    message(STATUS "  Shell template: ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
endif()

target_link_options(MulanGeoWasm PRIVATE ${EMSCRIPTEN_LINK_FLAGS})

# 输出 .html + .js + .wasm
set_target_properties(MulanGeoWasm PROPERTIES
    SUFFIX ".html"
)

message(STATUS "MulanGeoWasm: Emscripten WASM build configured")
message(STATUS "  Emscripten: $ENV{EMSDK}")
