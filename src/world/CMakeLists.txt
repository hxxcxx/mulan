# World - 领域实体层（静态库）
add_library(World STATIC
    entity.cpp
    world.cpp
    viewport.cpp
    scene_proxy.cpp
    static_draw_list.cpp
    dynamic_draw_list.cpp
    system/transform_system.cpp
    system/bounds_system.cpp
    system/render_system.cpp
)

# 构建时将头文件复制到 build/include/mulan/world/...
file(GLOB_RECURSE WORLD_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${WORLD_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/world/${rel}" COPYONLY)
endforeach()

target_include_directories(World PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(World PUBLIC Engine)

set_target_properties(World PROPERTIES OUTPUT_NAME mulan_world)

target_compile_definitions(World PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
