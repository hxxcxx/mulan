# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    core/view_context.cpp
    core/preview_layer.cpp
    capture/capture_batch.cpp
    capture/capture_image_encoder.cpp
    capture/capture_request.cpp
    capture/capture_service.cpp
    runtime/render_device_context.cpp
    runtime/render_thread.cpp
    runtime/render_executor.cpp
    runtime/render_session.cpp
    runtime/render_surface.cpp
    runtime/render_channel.cpp
    scene_sync/render_item_builder.cpp
    scene_sync/render_submission_builder.cpp
    scene_sync/render_world_sync.cpp
    scene_sync/geometry_query.cpp
    scene_sync/detail/mesh_picking.cpp
    scene_sync/detail/primitive_pick_index.cpp
    scene_sync/detail/curve_picking.cpp
    scene_sync/detail/asset_picking.cpp
    scene_sync/render_scene.cpp
)

mulan_copy_public_headers(view view
    capture/capture_batch.h
    capture/capture_image_encoder.h
    capture/capture_request.h
    core/preview_layer.h
    core/view_config.h
    core/view_context.h
    core/view_state.h
    scene_sync/render_scene.h
    scene_sync/scene_proxy.h
)

target_include_directories(view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(view PUBLIC
    render
    interaction
    rhi
    asset
    scene
)

target_link_libraries(view PRIVATE mulan::profiling)

target_precompile_headers(view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(view PROPERTIES OUTPUT_NAME mulan_view)

target_compile_definitions(view PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
