# Math - 自有数学库（静态库，非模板实现分离编译）
#
# 标量/线性代数/基础几何/求交/参数曲线曲面/2D 计算几何（凸包/定向谓词）。
# 形态：STATIC 库。模板类/轻量 inline 保留在头文件，非模板重型实现移至 .cpp。
add_library(math STATIC
    # 非模板重型实现分离编译
    algo/intersect.cpp
    algo2d/convex_hull.cpp
    algo2d/triangulation.cpp
    algo2d/voronoi.cpp
    algo2d/polygon_intersect.cpp
    basis/bspline_basis.cpp
    surface/bezier_surface.cpp
    surface/bspline_surface.cpp
    surface/nurbs_surface.cpp
    topology/dcel.cpp
    spatial/kdtree.cpp
    spatial/quadtree.cpp
    spatial/rtree.cpp
    spatial/bsptree.cpp
    spatial/bvhtree.cpp
)

# 构建时将头文件复制到 build/include/mulan/math/...（保持子目录结构）
file(GLOB_RECURSE MATH_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${MATH_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/math/${rel}" COPYONLY)
endforeach()

target_include_directories(math PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# STATIC 库：无需导出宏，MATH_API 退化为空
# 如需改为 SHARED，定义 BUILDING_MATH 启用 dllexport
