# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    view_context.cpp
    render_surface.cpp
    renderer.cpp
    draw_command_builder.cpp
    render_material_resolver.cpp
    render_scene.cpp
)

file(GLOB_RECURSE VIEW_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${VIEW_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/view/${rel}" COPYONLY)
endforeach()

target_include_directories(view PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(view PUBLIC
    engine
    asset
    scene
)

set_target_properties(view PROPERTIES OUTPUT_NAME mulan_view)

target_compile_definitions(view PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
