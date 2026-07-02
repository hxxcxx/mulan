# RenderScene - derived render cache boundary between Scene/Asset and Engine
add_library(RenderScene STATIC
    render_scene.cpp
)

file(GLOB_RECURSE RENDER_SCENE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${RENDER_SCENE_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/render_scene/${rel}" COPYONLY)
endforeach()

target_include_directories(RenderScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(RenderScene PUBLIC Scene Asset Engine)

set_target_properties(RenderScene PROPERTIES OUTPUT_NAME mulan_render_scene)

target_compile_definitions(RenderScene PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

