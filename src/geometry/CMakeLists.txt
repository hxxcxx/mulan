# Geometry - 几何内核（动态库）
#
# 几何特征接口 + NURBS 实现 + 解析几何 + 装饰器 + Newton 求解器
# 依赖: GLM (header-only)
add_library(Geometry SHARED
    Newton.cpp
    nurbs/KnotVec.cpp
)

# 构建时将头文件复制到 build/include/mulan/geometry/...
file(GLOB_RECURSE GEO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${GEO_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/geometry/${rel}" COPYONLY)
endforeach()

target_include_directories(Geometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# --- GLM (header-only math library, 与 Engine 共享) ---
find_package(glm CONFIG REQUIRED)
target_link_libraries(Geometry PUBLIC glm::glm)

# 导出宏
target_compile_definitions(Geometry PRIVATE
    BUILDING_GEOMETRY=1
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(Geometry PRIVATE /FS)
endif()
