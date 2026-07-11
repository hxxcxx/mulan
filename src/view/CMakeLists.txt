# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    core/view_context.cpp
    core/preview_layer.cpp
    capture/capture_batch.cpp
    capture/capture_image_encoder.cpp
    capture/capture_request.cpp
    capture/capture_service.cpp
    runtime/render_queue.cpp
    runtime/render_runtime.cpp
    runtime/render_runtime_host.cpp
    runtime/render_thread.cpp
    runtime/threaded_render_runtime.cpp
    scene_sync/render_item_builder.cpp
    scene_sync/render_submission_builder.cpp
    scene_sync/renderer.cpp
    scene_sync/render_world_sync.cpp
    scene_sync/geometry_query.cpp
    scene_sync/detail/mesh_picking.cpp
    scene_sync/detail/curve_picking.cpp
    scene_sync/detail/asset_picking.cpp
    scene_sync/render_scene.cpp
    scene_sync/render_surface.cpp
)

mulan_copy_public_headers(view view)

target_link_libraries(view PUBLIC
    render
    interaction
    rhi
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
