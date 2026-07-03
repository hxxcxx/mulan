# View - 消费 RenderScene 的视图运行时
add_library(View STATIC
    view_context.cpp
    render_surface.cpp
    renderer.cpp
    draw_command_builder.cpp
    render_material_resolver.cpp
)

file(GLOB_RECURSE VIEW_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${VIEW_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/view/${rel}" COPYONLY)
endforeach()

target_include_directories(View PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(View PUBLIC
    Engine
    Asset
    RenderScene
)

set_target_properties(View PROPERTIES OUTPUT_NAME mulan_view)

target_compile_definitions(View PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
