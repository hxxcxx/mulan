cmake_minimum_required(VERSION 3.24)
project(mulan VERSION 1.0.0 LANGUAGES C CXX)

# 当前构建目标支持 Windows 与 Linux。其他平台必须先完成明确的平台接入，
# 不能以 UNIX 分支静默落入 Linux 实现。
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Mulan currently supports Windows and Linux only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(CTest)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles([=[
        #include <expected>
        #include <format>
        #include <thread>
        #include <type_traits>
        int main() {
            std::expected<int, int> value = 1;
            static_assert(std::is_destructible_v<std::jthread>);
            return std::format("{}", *value).empty();
        }
    ]=] MULAN_HAS_REQUIRED_CXX23_LIBRARY)
    if(NOT MULAN_HAS_REQUIRED_CXX23_LIBRARY)
        message(FATAL_ERROR
            "The Linux toolchain must provide C++23 std::expected, std::format, and std::jthread support."
        )
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MulanHeaders)
include(MulanDeploy)
include(MulanShaders)

# MSVC: 源文件为 UTF-8，避免 C4819 编码警告
if(MSVC)
    # 即使 MSBuild 串行执行，Visual Studio 仍可能为单个 target 启动多个编译进程。
    # /FS 用于协调项目 target 和内嵌第三方 target 对共享 PDB 文件的写入。
    add_compile_options(/utf-8 /FS)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(MULAN_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

option(MULAN_ENABLE_DEV_RUNTIME_ENV "Configure debugger runtime paths for F5 without copying runtime DLLs." ${WIN32})
option(MULAN_ENABLE_POST_BUILD_DEPLOY "Copy runtime dependencies after every app build." OFF)
option(MULAN_ENABLE_RUNTIME_DEPLOY_TARGET "Create an explicit runtime deploy target for standalone runs." ${WIN32})
option(MULAN_DEPLOY_QT_WITH_WINDEPLOYQT "Run windeployqt from the explicit deploy target." ${WIN32})
option(MULAN_ENABLE_OCCT_BACKEND "Build the OpenCASCADE modeling backend." ${WIN32})
option(MULAN_ENABLE_TRUCK_BACKEND "Build the experimental truck-bridge modeling backend." OFF)
option(MULAN_SHADER_DEBUG_INFO "Emit debug info in compiled Slang shaders (symbol names, source line mapping for RenderDoc/PIX)." ON)
# Profiling 埋点只在 RelWithDebInfo 中启用。builtin 是日常默认的 Mulan Profiler，
# tracy 仅用于临时排查时间线/锁竞争，none 可完全移除埋点。
set(MULAN_PROFILER_BACKEND "builtin" CACHE STRING "Profiler backend: builtin, tracy, or none")
set_property(CACHE MULAN_PROFILER_BACKEND PROPERTY STRINGS builtin tracy none)
string(TOLOWER "${MULAN_PROFILER_BACKEND}" MULAN_PROFILER_BACKEND_NORMALIZED)
if(NOT MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "builtin"
   AND NOT MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "tracy"
   AND NOT MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "none")
    message(FATAL_ERROR "Unsupported MULAN_PROFILER_BACKEND='${MULAN_PROFILER_BACKEND}'")
endif()

set(MULAN_PROFILING_COMPILE_DEFINITIONS)
if(MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "none")
    list(APPEND MULAN_PROFILING_COMPILE_DEFINITIONS
        MULAN_ENABLE_PROFILING=0
        MULAN_PROFILER_BACKEND_NONE=1
    )
else()
    list(APPEND MULAN_PROFILING_COMPILE_DEFINITIONS
        $<$<CONFIG:RelWithDebInfo>:MULAN_ENABLE_PROFILING=1>
        $<$<NOT:$<CONFIG:RelWithDebInfo>>:MULAN_ENABLE_PROFILING=0>
    )
    if(MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "tracy")
        find_package(Tracy CONFIG REQUIRED)
        list(APPEND MULAN_PROFILING_COMPILE_DEFINITIONS MULAN_PROFILER_BACKEND_TRACY=1)
    else()
        list(APPEND MULAN_PROFILING_COMPILE_DEFINITIONS MULAN_PROFILER_BACKEND_BUILTIN=1)
    endif()
endif()

add_library(mulan_profiling INTERFACE)
add_library(mulan::profiling ALIAS mulan_profiling)
target_compile_definitions(mulan_profiling INTERFACE ${MULAN_PROFILING_COMPILE_DEFINITIONS})
target_link_libraries(mulan_profiling INTERFACE core)
if(MULAN_PROFILER_BACKEND_NORMALIZED STREQUAL "tracy")
    target_link_libraries(mulan_profiling INTERFACE Tracy::TracyClient)
endif()
message(STATUS "Profiler backend: ${MULAN_PROFILER_BACKEND_NORMALIZED}")

set(MULAN_DEFAULT_SHAPE_OPS_BACKEND "occt" CACHE STRING
    "Default IShapeOps backend used when no runtime environment override is set")
set_property(CACHE MULAN_DEFAULT_SHAPE_OPS_BACKEND PROPERTY STRINGS occt truck)

string(TOLOWER "${MULAN_DEFAULT_SHAPE_OPS_BACKEND}" MULAN_DEFAULT_SHAPE_OPS_BACKEND_NORMALIZED)
if(NOT MULAN_DEFAULT_SHAPE_OPS_BACKEND_NORMALIZED STREQUAL "occt"
   AND NOT MULAN_DEFAULT_SHAPE_OPS_BACKEND_NORMALIZED STREQUAL "truck")
    message(FATAL_ERROR
        "Unsupported MULAN_DEFAULT_SHAPE_OPS_BACKEND='${MULAN_DEFAULT_SHAPE_OPS_BACKEND}'. "
        "Expected occt or truck."
    )
endif()

set(MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND "${MULAN_DEFAULT_SHAPE_OPS_BACKEND_NORMALIZED}")
if(NOT MULAN_ENABLE_TRUCK_BACKEND AND MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND STREQUAL "truck")
    if(MULAN_ENABLE_OCCT_BACKEND)
        set(MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND "occt")
    else()
        set(MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND "none")
    endif()
    message(STATUS "truck backend is disabled; defaulting IShapeOps to ${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}")
elseif(NOT MULAN_ENABLE_OCCT_BACKEND AND MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND STREQUAL "occt")
    if(MULAN_ENABLE_TRUCK_BACKEND)
        set(MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND "truck")
    else()
        set(MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND "none")
    endif()
    message(STATUS "OCCT backend is disabled; defaulting IShapeOps to ${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}")
endif()
message(STATUS "Default IShapeOps backend: ${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}")

set(MULAN_TRUCK_BRIDGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/truck-bridge" CACHE PATH
    "truck-bridge source directory")

if(MULAN_ENABLE_TRUCK_BACKEND)
    if(NOT WIN32)
        message(FATAL_ERROR "The experimental truck backend is not available on Linux yet.")
    endif()

    find_program(CARGO_EXECUTABLE cargo REQUIRED)

    if(NOT EXISTS "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/Cargo.toml")
        message(FATAL_ERROR "truck-bridge Cargo.toml not found: ${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/Cargo.toml")
    endif()

    file(GLOB_RECURSE MULAN_TRUCK_BRIDGE_RUST_SOURCES CONFIGURE_DEPENDS
        "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/src/*.rs"
    )

    set(MULAN_TRUCK_BRIDGE_TARGET_DIR "${CMAKE_BINARY_DIR}/truck-bridge-target")
    set(MULAN_TRUCK_BRIDGE_INCLUDE_DIR "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/include")
    set(MULAN_TRUCK_BRIDGE_HEADER "${MULAN_TRUCK_BRIDGE_INCLUDE_DIR}/truck_bridge.h")
    set(MULAN_TRUCK_BRIDGE_DLL "${MULAN_TRUCK_BRIDGE_TARGET_DIR}/release/truck_bridge.dll")
    set(MULAN_TRUCK_BRIDGE_IMPLIB "${MULAN_TRUCK_BRIDGE_TARGET_DIR}/release/truck_bridge.dll.lib")
    file(MAKE_DIRECTORY "${MULAN_TRUCK_BRIDGE_INCLUDE_DIR}")

    add_custom_command(
        OUTPUT
            "${MULAN_TRUCK_BRIDGE_DLL}"
            "${MULAN_TRUCK_BRIDGE_IMPLIB}"
            "${MULAN_TRUCK_BRIDGE_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env
            "CARGO_TARGET_DIR=${MULAN_TRUCK_BRIDGE_TARGET_DIR}"
            "${CARGO_EXECUTABLE}" build --release --locked
                --manifest-path "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/Cargo.toml"
        DEPENDS
            "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/Cargo.toml"
            "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/Cargo.lock"
            "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/build.rs"
            "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}/cbindgen.toml"
            ${MULAN_TRUCK_BRIDGE_RUST_SOURCES}
        WORKING_DIRECTORY "${MULAN_TRUCK_BRIDGE_SOURCE_DIR}"
        COMMENT "Building truck-bridge with Cargo..."
        VERBATIM
    )

    add_custom_target(truck_bridge_build
        DEPENDS
            "${MULAN_TRUCK_BRIDGE_DLL}"
            "${MULAN_TRUCK_BRIDGE_IMPLIB}"
            "${MULAN_TRUCK_BRIDGE_HEADER}"
    )

    add_library(truck_bridge SHARED IMPORTED GLOBAL)
    set_target_properties(truck_bridge PROPERTIES
        IMPORTED_LOCATION "${MULAN_TRUCK_BRIDGE_DLL}"
        IMPORTED_IMPLIB "${MULAN_TRUCK_BRIDGE_IMPLIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${MULAN_TRUCK_BRIDGE_INCLUDE_DIR}"
    )
    add_dependencies(truck_bridge truck_bridge_build)
endif()

# ── OpenCASCADE ──
# Windows 使用 OCCT 官方预编译包，Linux 使用系统开发包。两端均从官方 CMake
# package 取得真实的头文件、库与传递依赖，不再在项目中硬编译工具链目录。
if(MULAN_ENABLE_OCCT_BACKEND)
    # OCCT 官方 package 会向当前 CMake 目录注入其自身的编译宏。本项目只需要
    # imported targets，因此保存并恢复目录属性，避免 HAVE_* 等宏污染其他模块。
    get_property(MULAN_COMPILE_DEFINITIONS_BEFORE_OCCT DIRECTORY PROPERTY COMPILE_DEFINITIONS)
    # DataExchange 的官方导出集会引用 Visualization/ApplicationFramework targets；
    # 必须一并导入它们，但下方只链接建模与 STEP/IGES 实际所需的 toolkit。
    set(MULAN_OCCT_COMPONENTS
        FoundationClasses
        ModelingData
        ModelingAlgorithms
        Visualization
        ApplicationFramework
        DataExchange
    )

    if(WIN32)
        # 查找路径优先级: CMakePresets / -DOCCT_ROOT / 环境变量 OCCT_ROOT。
        set(MULAN_OCCT_ROOT_CANDIDATE "${OCCT_ROOT}")
        if((NOT MULAN_OCCT_ROOT_CANDIDATE OR NOT EXISTS "${MULAN_OCCT_ROOT_CANDIDATE}") AND DEFINED ENV{OCCT_ROOT})
            if(MULAN_OCCT_ROOT_CANDIDATE AND NOT EXISTS "${MULAN_OCCT_ROOT_CANDIDATE}")
                message(WARNING "OCCT_ROOT is set to '${MULAN_OCCT_ROOT_CANDIDATE}' but the directory does not exist, falling back...")
            endif()
            set(MULAN_OCCT_ROOT_CANDIDATE "$ENV{OCCT_ROOT}")
        endif()
        if(NOT MULAN_OCCT_ROOT_CANDIDATE OR NOT EXISTS "${MULAN_OCCT_ROOT_CANDIDATE}")
            message(FATAL_ERROR
                "OCCT_ROOT not set or directory does not exist. "
                "Use CMakePresets.json, -DOCCT_ROOT=..., or set OCCT_ROOT env var."
            )
        endif()
        set(OCCT_ROOT "${MULAN_OCCT_ROOT_CANDIDATE}" CACHE PATH "OpenCASCADE root directory" FORCE)
        set(OpenCASCADE_DIR "${OCCT_ROOT}/cmake" CACHE PATH "OpenCASCADE CMake package directory" FORCE)
        if(NOT EXISTS "${OpenCASCADE_DIR}/OpenCASCADEConfig.cmake")
            message(FATAL_ERROR "OpenCASCADEConfig.cmake not found under OCCT_ROOT: ${OpenCASCADE_DIR}")
        endif()
        find_package(OpenCASCADE CONFIG REQUIRED COMPONENTS ${MULAN_OCCT_COMPONENTS}
            PATHS "${OpenCASCADE_DIR}" NO_DEFAULT_PATH)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Ubuntu 的 OCCT imported targets 直接引用 TBB 开发链接文件，但其 OCCT
        # -dev 包只保证 TBB runtime。在配置期明确验证 libtbb-dev，避免延迟到 Ninja 链接失败。
        find_package(TBB CONFIG REQUIRED COMPONENTS tbb tbbmalloc)
        # OCCT 的官方 version file 使用 ExactVersion 策略，不接受 "7.6" 作为
        # 7.6.3 的兼容下界；先查找 package，下方再按平台验证版本区间。
        find_package(OpenCASCADE CONFIG REQUIRED COMPONENTS ${MULAN_OCCT_COMPONENTS})
    else()
        message(FATAL_ERROR "The OCCT backend is not supported on ${CMAKE_SYSTEM_NAME}.")
    endif()
    set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS "${MULAN_COMPILE_DEFINITIONS_BEFORE_OCCT}")

    set(OCCT_VERSION
        "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
    if(WIN32 AND (OCCT_VERSION VERSION_LESS "7.9" OR NOT OCCT_VERSION VERSION_LESS "8.0"))
        message(FATAL_ERROR "Unsupported Windows OCCT version ${OCCT_VERSION}; expected 7.9.x")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux"
           AND (OCCT_VERSION VERSION_LESS "7.6" OR NOT OCCT_VERSION VERSION_LESS "7.7"))
        message(FATAL_ERROR "Unsupported Linux OCCT version ${OCCT_VERSION}; expected 7.6.x")
    endif()
    message(STATUS "OCCT version: ${OCCT_VERSION}")
    message(STATUS "OCCT include directory: ${OpenCASCADE_INCLUDE_DIR}")

    if(OpenCASCADE_MINOR_VERSION GREATER_EQUAL 8)
        set(OCCT_LIBS
            TKernel TKMath
            TKG2d TKG3d TKGeomBase TKBRep
            TKGeomAlgo TKTopAlgo TKPrim TKBO TKShHealing TKMesh
            TKDE TKXSBase TKDESTEP TKDEIGES
        )
    else()
        set(OCCT_LIBS
            TKernel TKMath
            TKG2d TKG3d TKGeomBase TKBRep
            TKGeomAlgo TKTopAlgo TKPrim TKBO TKShHealing TKMesh
            TKXSBase TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKIGES
        )
    endif()
    foreach(lib IN LISTS OCCT_LIBS)
        if(NOT TARGET ${lib})
            message(FATAL_ERROR "Required OCCT imported target is missing: ${lib}")
        endif()
    endforeach()

    add_library(mulan_occt INTERFACE)
    add_library(OCCT::OCCT ALIAS mulan_occt)
    # Ubuntu 7.6 的 imported targets 没有携带 include directory，Windows 7.9 则携带。
    # 使用 package 公开的统一变量补齐，不猜测平台目录。
    target_include_directories(mulan_occt INTERFACE "${OpenCASCADE_INCLUDE_DIR}")
    target_link_libraries(mulan_occt INTERFACE ${OCCT_LIBS})

    if(WIN32)
        set(OCCT_BIN_DIR "${OpenCASCADE_BINARY_DIR}")
        set(OCCT_RUNTIME_REQUIRED_EXTRA_DLLS
            TKBool TKCAF TKCDF TKHLR TKLCAF TKService TKV3d TKVCAF TKXCAF
        )
        set(OCCT_RUNTIME_USER_EXTRA_DLLS "" CACHE STRING
            "Additional OCCT runtime DLL names copied next to the app executable")
        set(OCCT_RUNTIME_EXTRA_DLLS
            ${OCCT_RUNTIME_REQUIRED_EXTRA_DLLS}
            ${OCCT_RUNTIME_USER_EXTRA_DLLS}
        )
        set(OCCT_RUNTIME_DLL_FILES)
        foreach(lib IN LISTS OCCT_LIBS OCCT_RUNTIME_EXTRA_DLLS)
            set(OCCT_RUNTIME_DLL "${OCCT_BIN_DIR}/${lib}.dll")
            if(EXISTS "${OCCT_RUNTIME_DLL}")
                list(APPEND OCCT_RUNTIME_DLL_FILES "${OCCT_RUNTIME_DLL}")
            else()
                message(WARNING "OCCT runtime DLL not found: ${OCCT_RUNTIME_DLL}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES OCCT_RUNTIME_DLL_FILES)

        set(MULAN_OCCT_REQUIRED_3RDPARTY_DLL_NAMES
            avcodec-57.dll avformat-57.dll avutil-55.dll FreeImage.dll freetype.dll
            jemalloc.dll openvr_api.dll swscale-4.dll tbb12.dll
        )
        set(MULAN_OCCT_EXTRA_3RDPARTY_DLL_NAMES "" CACHE STRING
            "Additional OCCT third-party runtime DLL names copied next to the app executable")
        unset(MULAN_OCCT_3RDPARTY_DLL_NAMES CACHE)
        set(MULAN_OCCT_3RDPARTY_DLL_NAMES
            ${MULAN_OCCT_REQUIRED_3RDPARTY_DLL_NAMES}
            ${MULAN_OCCT_EXTRA_3RDPARTY_DLL_NAMES}
        )
        list(REMOVE_DUPLICATES MULAN_OCCT_3RDPARTY_DLL_NAMES)

        set(MULAN_OCCT_3RDPARTY_CANDIDATE "${OCCT_3RDPARTY_DIR}")
        if((NOT MULAN_OCCT_3RDPARTY_CANDIDATE OR NOT EXISTS "${MULAN_OCCT_3RDPARTY_CANDIDATE}")
           AND DEFINED ENV{OCCT_3RDPARTY_DIR})
            set(MULAN_OCCT_3RDPARTY_CANDIDATE "$ENV{OCCT_3RDPARTY_DIR}")
        endif()
        if(MULAN_OCCT_3RDPARTY_CANDIDATE AND EXISTS "${MULAN_OCCT_3RDPARTY_CANDIDATE}")
            set(OCCT_3RDPARTY_DIR "${MULAN_OCCT_3RDPARTY_CANDIDATE}" CACHE PATH
                "OpenCASCADE third-party runtime directory" FORCE)
            message(STATUS "OCCT_3RDPARTY_DIR: ${OCCT_3RDPARTY_DIR}")
        elseif(MULAN_OCCT_3RDPARTY_CANDIDATE)
            message(WARNING
                "OCCT_3RDPARTY_DIR is set to '${MULAN_OCCT_3RDPARTY_CANDIDATE}' but the directory does not exist.")
        endif()
    endif()
endif()

# ── Qt6 ──
# Qt 由 vcpkg manifest 统一提供；builtin-baseline 锁定当前使用的 6.11.1 版本。
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Widgets Svg)
message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "Target platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(MSVC)
    message(STATUS "MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

# ── 3rdparty ──
add_subdirectory(3rdparty/SARibbon)

# msdf-atlas-gen: 使用内嵌的 msdfgen 子模块，不编译独立可执行文件
set(MSDF_ATLAS_MSDFGEN_EXTERNAL OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "" FORCE)
set(MSDF_ATLAS_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/msdf-atlas-gen)

# ── Subdirectories ──
add_subdirectory(src/core)
add_subdirectory(src/math)
add_subdirectory(src/graphics)
add_subdirectory(src/engine)
add_subdirectory(src/modeling/core)
add_subdirectory(src/asset)
add_subdirectory(src/scene)
add_subdirectory(src/document)
if(MULAN_ENABLE_OCCT_BACKEND)
    add_subdirectory(src/modeling/occt)
endif()
if(MULAN_ENABLE_TRUCK_BACKEND)
    add_subdirectory(src/modeling/truck)
endif()
add_subdirectory(src/view)
add_subdirectory(src/io)
add_subdirectory(src/modeling/runtime)
add_subdirectory(src/editor)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
