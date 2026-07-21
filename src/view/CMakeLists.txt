# View - 消费 RenderScene 的视图运行时
add_library(view STATIC
    core/view_context.cpp
    core/preview_layer.cpp
    core/view_cube_model.cpp
    capture/capture_batch.cpp
    capture/capture_image_encoder.cpp
    capture/capture_request.cpp
    capture/capture_service.cpp
    runtime/view_render_bridge.cpp
    scene_sync/projection/render_item_builder.cpp
    scene_sync/projection/render_resource_delta.cpp
    scene_sync/projection/render_resource_outbox.cpp
    scene_sync/projection/render_submission_builder.cpp
    scene_sync/projection/render_world_sync.cpp
    scene_sync/picking/geometry_query.cpp
    scene_sync/picking/mesh_picking.cpp
    scene_sync/picking/primitive_pick_index.cpp
    scene_sync/picking/curve_picking.cpp
    scene_sync/picking/asset_picking.cpp
    scene_sync/render_scene.cpp
    scene_sync/render_scene_sync.cpp
)

mulan_copy_public_headers(view view
    capture/capture_batch.h
    capture/capture_image_encoder.h
    capture/capture_request.h
    core/preview_layer.h
    core/view_config.h
    core/view_context.h
    core/view_state.h
    core/view_cube_model.h
    scene_sync/render_scene.h
    scene_sync/scene_proxy.h
)

target_include_directories(view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(view PUBLIC
    render
    interaction
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
