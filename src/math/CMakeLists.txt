# Math - 自有数学库（header-only，零外部依赖）
#
# 标量/线性代数/基础几何/求交。分子目录：scalar / linalg / geom / algo / curve。
add_library(math INTERFACE)

# 构建时将头文件复制到 build/include/mulan/math/...（保持子目录结构）
file(GLOB_RECURSE MATH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${MATH_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/math/${rel}" COPYONLY)
endforeach()

target_include_directories(math INTERFACE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# header-only：BUILDING_MATH 无需定义，MATH_API 退化为空
