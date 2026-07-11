# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    capture_batch.cpp
    capture_image_encoder.cpp
    capture_request.cpp
    capture_service.cpp
    preview_layer.cpp
    view_context.cpp
    render_queue.cpp
    render_runtime.cpp
    render_runtime_host.cpp
    render_surface.cpp
    render_thread.cpp
    threaded_render_runtime.cpp
    render_item_builder.cpp
    render_submission_builder.cpp
    renderer.cpp
    render_world_sync.cpp
    geometry_query.cpp
    render_scene.cpp
)

mulan_copy_public_headers(view view)

target_link_libraries(view PUBLIC
    render
    interaction
    rhi
    platform
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
