cmake_minimum_required(VERSION 3.24)
project(mulan VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MulanHeaders)
include(MulanDeploy)
include(MulanShaders)

# MSVC: 源文件为 UTF-8，避免 C4819 编码警告
if(MSVC)
    add_compile_options(/utf-8)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(MULAN_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

option(MULAN_ENABLE_DEV_RUNTIME_ENV "Configure debugger runtime paths for F5 without copying runtime DLLs." ON)
option(MULAN_ENABLE_DEV_RUNTIME_COPY "Copy the minimal runtime set needed for F5/debug runs." ON)
option(MULAN_ENABLE_POST_BUILD_DEPLOY "Copy runtime dependencies after every app build." OFF)
option(MULAN_ENABLE_RUNTIME_DEPLOY_TARGET "Create an explicit runtime deploy target for standalone runs." ON)
option(MULAN_DEPLOY_QT_WITH_WINDEPLOYQT "Run windeployqt from the explicit deploy target." ON)

# ── OpenCASCADE ──
# 查找路径优先级: CMakePresets / -DOCCT_ROOT / 环境变量 OCCT_ROOT
# 即使变量已设置，也需要验证目录是否真实存在
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
        "Use CMakePresets.json, -DOCCT_ROOT=..., or set OCCT_ROOT env var, "
        "and make sure the path points to an existing directory."
    )
endif()
set(OCCT_ROOT "${MULAN_OCCT_ROOT_CANDIDATE}" CACHE PATH "OpenCASCADE root directory" FORCE)
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
    TKDE TKXSBase TKDESTEP TKDEIGES
)

set(OCCT_RUNTIME_REQUIRED_EXTRA_DLLS
    TKBool
    TKCAF
    TKCDF
    TKHLR
    TKLCAF
    TKService
    TKV3d
    TKVCAF
    TKXCAF
)
set(OCCT_RUNTIME_USER_EXTRA_DLLS "" CACHE STRING "Additional OCCT runtime DLL names copied next to the app executable")
set(OCCT_RUNTIME_EXTRA_DLLS
    ${OCCT_RUNTIME_REQUIRED_EXTRA_DLLS}
    ${OCCT_RUNTIME_USER_EXTRA_DLLS}
)

set(OCCT_LIBRARY_FILES)
set(OCCT_MISSING_LIBS)
set(OCCT_RUNTIME_DLL_FILES)
foreach(lib IN LISTS OCCT_LIBS)
    find_library(OCCT_LIBRARY_${lib} NAMES ${lib} PATHS "${OCCT_LIB_DIR}" NO_DEFAULT_PATH)
    if(OCCT_LIBRARY_${lib})
        list(APPEND OCCT_LIBRARY_FILES "${OCCT_LIBRARY_${lib}}")
    else()
        list(APPEND OCCT_MISSING_LIBS "${lib}")
    endif()

    set(OCCT_RUNTIME_DLL "${OCCT_BIN_DIR}/${lib}.dll")
    if(EXISTS "${OCCT_RUNTIME_DLL}")
        list(APPEND OCCT_RUNTIME_DLL_FILES "${OCCT_RUNTIME_DLL}")
    endif()
endforeach()
foreach(lib IN LISTS OCCT_RUNTIME_EXTRA_DLLS)
    set(OCCT_RUNTIME_DLL "${OCCT_BIN_DIR}/${lib}.dll")
    if(EXISTS "${OCCT_RUNTIME_DLL}")
        list(APPEND OCCT_RUNTIME_DLL_FILES "${OCCT_RUNTIME_DLL}")
    else()
        message(WARNING "OCCT runtime DLL not found: ${OCCT_RUNTIME_DLL}")
    endif()
endforeach()
list(REMOVE_DUPLICATES OCCT_RUNTIME_DLL_FILES)
if(OCCT_MISSING_LIBS)
    list(JOIN OCCT_MISSING_LIBS ", " OCCT_MISSING_LIBS_TEXT)
    message(FATAL_ERROR "OCCT libraries not found in ${OCCT_LIB_DIR}: ${OCCT_MISSING_LIBS_TEXT}")
endif()

add_library(mulan_occt INTERFACE)
add_library(OCCT::OCCT ALIAS mulan_occt)
target_include_directories(mulan_occt INTERFACE "${OCCT_INCLUDE_DIR}")
target_link_libraries(mulan_occt INTERFACE ${OCCT_LIBRARY_FILES})

set(MULAN_OCCT_REQUIRED_3RDPARTY_DLL_NAMES
    avcodec-57.dll
    avformat-57.dll
    avutil-55.dll
    FreeImage.dll
    freetype.dll
    jemalloc.dll
    openvr_api.dll
    swscale-4.dll
    tbb12.dll
)
set(MULAN_OCCT_EXTRA_3RDPARTY_DLL_NAMES "" CACHE STRING "Additional OCCT third-party runtime DLL names copied next to the app executable")
unset(MULAN_OCCT_3RDPARTY_DLL_NAMES CACHE)
set(MULAN_OCCT_3RDPARTY_DLL_NAMES
    ${MULAN_OCCT_REQUIRED_3RDPARTY_DLL_NAMES}
    ${MULAN_OCCT_EXTRA_3RDPARTY_DLL_NAMES}
)
list(REMOVE_DUPLICATES MULAN_OCCT_3RDPARTY_DLL_NAMES)

# ── OCCT 3rdparty ──
set(MULAN_OCCT_3RDPARTY_CANDIDATE "${OCCT_3RDPARTY_DIR}")
if((NOT MULAN_OCCT_3RDPARTY_CANDIDATE OR NOT EXISTS "${MULAN_OCCT_3RDPARTY_CANDIDATE}") AND DEFINED ENV{OCCT_3RDPARTY_DIR})
    set(MULAN_OCCT_3RDPARTY_CANDIDATE "$ENV{OCCT_3RDPARTY_DIR}")
endif()
if(MULAN_OCCT_3RDPARTY_CANDIDATE AND EXISTS "${MULAN_OCCT_3RDPARTY_CANDIDATE}")
    set(OCCT_3RDPARTY_DIR "${MULAN_OCCT_3RDPARTY_CANDIDATE}" CACHE PATH "OpenCASCADE third-party runtime directory" FORCE)
    message(STATUS "OCCT_3RDPARTY_DIR: ${OCCT_3RDPARTY_DIR}")
elseif(MULAN_OCCT_3RDPARTY_CANDIDATE)
    message(WARNING "OCCT_3RDPARTY_DIR is set to '${MULAN_OCCT_3RDPARTY_CANDIDATE}' but the directory does not exist.")
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
add_subdirectory(src/math)
add_subdirectory(src/graphics)
add_subdirectory(src/engine)
add_subdirectory(src/asset)
add_subdirectory(src/scene)
add_subdirectory(src/view)
add_subdirectory(src/io)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
