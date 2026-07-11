# editor - 编辑逻辑层：工具/选择/捕捉/夹点/会话/操作/命令/文档桥接。
# 零 Qt 依赖，可在无 Qt 环境下编译测试。
add_library(editor STATIC
    # command/
    command/command_manager.cpp
    command/builtin_commands.cpp
    # core/ — 编辑核心：工具/选择/捕捉/夹点/会话/操作
    core/command_history.cpp
    core/document_operation.cpp
    core/document_operation_executor.cpp
    core/control_polygon_builder.cpp
    core/drag_edit_session.cpp
    core/draft_geometry.cpp
    core/editor_action.cpp
    core/editor_grip_controller.cpp
    core/editor_overlay_service.cpp
    core/editor_preview_controller.cpp
    core/editor_input_resolver.cpp
    core/editor_pick_service.cpp
    core/editor_pick_query_world.cpp
    core/editor_render_pick_conversion.cpp
    core/editor_grip_provider.cpp
    core/editor_scene_snap_provider.cpp
    core/editor_selection.cpp
    core/editor_selection_service.cpp
    core/editor_snap_collector.cpp
    core/editor_snap_resolver.cpp
    core/editor_session.cpp
    core/geometry_edit_service.cpp
    core/point_drawing_tool.cpp
    core/tool_controller.cpp
    core/transform_edit_context.cpp
    core/transform_tool.cpp
    core/param_curve_tool.cpp
    core/polyline_tool.cpp
    core/circle_tool.cpp
    core/face_tool.cpp
    core/extrude_tool.cpp
    core/selection_extrude_tool.cpp
    core/line_tool.cpp
    core/grip_drag_tool.cpp
    core/grip_marker_builder.cpp
    core/snap_marker_builder.cpp
    # document/ — 纯逻辑文档桥接层
    document/document_session.cpp
    document/document_render_cache.cpp
    document/document_render_binding.cpp
    document/document_pick_bridge.cpp
    document/document_selection_bridge.cpp
    document/document_view.cpp
    document/document_view_binding.cpp
)

target_include_directories(editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(editor PUBLIC
    view
    render
    interaction
    rhi
    asset
    scene
    io
    modeling_core
    runtime
)

target_precompile_headers(editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/pch.h
)

set_target_properties(editor PROPERTIES OUTPUT_NAME mulan_editor)

target_compile_definitions(editor PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
