cmake_minimum_required(VERSION 3.20)
project(mulan VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: 源文件为 UTF-8，避免 C4819 编码警告
if(MSVC)
    add_compile_options(/utf-8)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── OpenCASCADE ──
# 查找路径优先级: CMakePresets / -DOCCT_ROOT / 环境变量 OCCT_ROOT
# 即使变量已设置，也需要验证目录是否真实存在
if(NOT OCCT_ROOT OR NOT EXISTS "${OCCT_ROOT}")
    if(OCCT_ROOT AND NOT EXISTS "${OCCT_ROOT}")
        message(WARNING "OCCT_ROOT is set to '${OCCT_ROOT}' but the directory does not exist, falling back...")
    endif()
    if(DEFINED ENV{OCCT_ROOT} AND EXISTS "$ENV{OCCT_ROOT}")
        set(OCCT_ROOT "$ENV{OCCT_ROOT}")
    else()
        message(FATAL_ERROR
            "OCCT_ROOT not set or directory does not exist. "
            "Use CMakePresets.json, -DOCCT_ROOT=..., or set OCCT_ROOT env var, "
            "and make sure the path points to an existing directory."
        )
    endif()
endif()
message(STATUS "OCCT_ROOT: ${OCCT_ROOT}")

set(OCCT_INCLUDE_DIR "${OCCT_ROOT}/inc")
set(OCCT_LIB_DIR "${OCCT_ROOT}/win64/vc14/lib")
set(OCCT_BIN_DIR "${OCCT_ROOT}/win64/vc14/bin")

if(NOT EXISTS "${OCCT_INCLUDE_DIR}")
    message(FATAL_ERROR "OCCT include dir not found: ${OCCT_INCLUDE_DIR}")
endif()

set(OCCT_LIBS
    TKernel TKMath
    TKG2d TKG3d TKGeomBase TKBRep
    TKGeomAlgo TKTopAlgo TKPrim TKBO TKShHealing TKMesh
    TKCDF TKLCAF TKCAF TKBinL TKBin TKStdL TKStd TKVCAF
    TKDE TKXSBase TKDESTEP TKXCAF TKDEIGES TKDESTL TKRWMesh TKDECascade TKBinXCAF
)

# ── OCCT 3rdparty ──
if(NOT OCCT_3RDPARTY_DIR)
    if(DEFINED ENV{OCCT_3RDPARTY_DIR})
        set(OCCT_3RDPARTY_DIR "$ENV{OCCT_3RDPARTY_DIR}")
    endif()
endif()
if(OCCT_3RDPARTY_DIR)
    message(STATUS "OCCT_3RDPARTY_DIR: ${OCCT_3RDPARTY_DIR}")
endif()

# ── Qt6 ──
# 查找路径优先级: CMakePresets / -DCMAKE_PREFIX_PATH / Qt6_DIR 环境变量
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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
add_subdirectory(src/geo)
add_subdirectory(src/engine)
add_subdirectory(src/asset)
add_subdirectory(src/scene)
add_subdirectory(src/render_scene)
add_subdirectory(src/view)
add_subdirectory(src/document)
add_subdirectory(src/io)
add_subdirectory(src/app)
