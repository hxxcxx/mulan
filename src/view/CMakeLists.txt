# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    capture_batch.cpp
    capture_image_encoder.cpp
    capture_request.cpp
    capture_service.cpp
    view_context.cpp
    render_queue.cpp
    render_runtime.cpp
    render_runtime_host.cpp
    render_surface.cpp
    render_thread.cpp
    renderer.cpp
    render_world_sync.cpp
    render_scene.cpp
)

set(VIEW_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/mulan/view")
file(REMOVE_RECURSE "${VIEW_GENERATED_INCLUDE_DIR}")
file(GLOB_RECURSE VIEW_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${VIEW_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${VIEW_GENERATED_INCLUDE_DIR}/${rel}" COPYONLY)
endforeach()

target_include_directories(view PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(view PUBLIC
    engine
    asset
    scene
)

target_precompile_headers(view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(view PROPERTIES OUTPUT_NAME mulan_view)

target_compile_definitions(view PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
