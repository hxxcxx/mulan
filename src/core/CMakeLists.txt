# Core - 基础设施层（动态库）
#
# 图片加载/保存、日志、文件 IO 等通用功能。
# 所有模块均可引入。
add_library(core SHARED
    concurrency/thread_pool.cpp
    image/image.cpp
    image/stb_impl.cpp
    ini/config_file.cpp
    log/log.cpp
    profiling/profile.cpp
    result/error.cpp
)

# 构建时将头文件复制到 build/include/mulan/core/...
mulan_copy_public_headers(core core)

# --- stb (图像加载) ---
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(STB_INCLUDE_DIRS)
    target_include_directories(core PRIVATE ${STB_INCLUDE_DIRS})
else()
    message(WARNING "stb headers not found — Image.cpp will fail to compile")
endif()

# --- spdlog (日志) ---
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(core PRIVATE spdlog::spdlog)

# core 自身不能反向链接 mulan::profiling（该接口会把 core 传递给其他模块）。
target_compile_definitions(core PRIVATE ${MULAN_PROFILING_COMPILE_DEFINITIONS})
if(MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "tracy")
    target_link_libraries(core PRIVATE Tracy::TracyClient)
endif()

# --- toml++ (TOML 配置文件) ---
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(core PUBLIC tomlplusplus::tomlplusplus)

# CMake 对 SHARED 库自动定义 <name>_EXPORTS 宏（如 core_EXPORTS）
# 我们用 core_EXPORTS 来触发 dllexport
target_precompile_headers(core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(core PRIVATE
    BUILDING_CORE=1
    $<$<CONFIG:Debug>:_DEBUG>
)

set_target_properties(core PROPERTIES OUTPUT_NAME mulan_core)

if(MSVC)
    target_compile_options(core PRIVATE /FS)
endif()
