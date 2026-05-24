# Core - 基础设施层（动态库）
#
# 图片加载/保存、日志、文件 IO 等通用功能。
# 所有模块均可引入。
add_library(Core SHARED
    image/Image.cpp
    log/Log.cpp
    result/Result.cpp
    serialization/Archive.cpp
    serialization/BinaryArchive.cpp
    serialization/VersionBlock.cpp
    reflection/Object.cpp
    reflection/TypeRegistry.cpp
)

# 构建时将头文件复制到 build/include/mulan/core/...
file(GLOB_RECURSE CORE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/**/*.h"
)
foreach(hdr ${CORE_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/core/${rel}" COPYONLY)
endforeach()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/CoreExport.h"
               "${CMAKE_BINARY_DIR}/include/mulan/core/CoreExport.h" COPYONLY)


target_include_directories(Core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# --- stb (图像加载) ---
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(STB_INCLUDE_DIRS)
    target_include_directories(Core PRIVATE ${STB_INCLUDE_DIRS})
else()
    message(WARNING "stb headers not found — Image.cpp will fail to compile")
endif()

# --- spdlog (日志) ---
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(Core PRIVATE spdlog::spdlog)

# --- tl-expected (Result<T, Error>) ---
find_path(TL_EXPECTED_INCLUDE_DIR "tl/expected.hpp")
if(TL_EXPECTED_INCLUDE_DIR)
    target_include_directories(Core PUBLIC ${TL_EXPECTED_INCLUDE_DIR})
else()
    message(FATAL_ERROR "tl-expected not found — run vcpkg install tl-expected")
endif()

# CMake 对 SHARED 库自动定义 <name>_EXPORTS 宏（如 Core_EXPORTS）
# 我们用 Core_EXPORTS 来触发 dllexport
target_compile_definitions(Core PRIVATE
    BUILDING_CORE=1
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(Core PRIVATE /FS)
endif()
