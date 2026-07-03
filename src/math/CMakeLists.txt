# Geo - 自有几何数学库（header-only，零外部依赖）
#
# 向量/矩阵/四元数/基础几何/求交。本次仅建库，不接入业务模块。
add_library(Geo INTERFACE)

# 构建时将头文件复制到 build/include/mulan/geo/...
file(GLOB_RECURSE GEO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${GEO_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/geo/${rel}" COPYONLY)
endforeach()

target_include_directories(Geo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# header-only：BUILDING_GEO 无需定义，GEO_API 退化为空
