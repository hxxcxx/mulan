# editor - 编辑逻辑层：工具/选择/捕捉/夹点/会话/操作/命令/文档桥接。
# 零 Qt 依赖，可在无 Qt 环境下编译测试。
add_library(editor STATIC
    # command/ — 命令管理
    command/command_manager.cpp
    command/builtin_commands.cpp
    # core/operation/ — 操作历史与几何修改
    core/operation/command_history.cpp
    core/operation/document_operation.cpp
    core/operation/document_operation_executor.cpp
    core/operation/geometry_edit_service.cpp
    core/operation/draft_geometry.cpp
    core/operation/drag_edit_session.cpp
    core/operation/transform_edit_context.cpp
    core/operation/control_polygon_builder.cpp
    core/operation/editor_action.cpp
    # core/tools/ — 绘制与编辑工具
    core/tools/point_drawing_tool.cpp
    core/tools/line_tool.cpp
    core/tools/polyline_tool.cpp
    core/tools/circle_tool.cpp
    core/tools/face_tool.cpp
    core/tools/extrude_tool.cpp
    core/tools/selection_extrude_tool.cpp
    core/tools/param_curve_tool.cpp
    core/tools/grip_drag_tool.cpp
    core/tools/transform_tool.cpp
    core/tools/tool_controller.cpp
    core/tools/editor_tool_operator.cpp
    core/tools/drag_from_anchor_tool.cpp
    # core/selection/ — 选择与拾取
    core/selection/editor_selection.cpp
    core/selection/editor_selection_service.cpp
    core/selection/editor_pick_service.cpp
    core/selection/editor_pick_query_world.cpp
    core/selection/editor_render_pick_conversion.cpp
    core/selection/editor_scene_snap_provider.cpp
    # core/snap/ — 捕捉
    core/snap/editor_snap_collector.cpp
    core/snap/editor_snap_resolver.cpp
    core/snap/snap_marker_builder.cpp
    core/snap/editor_input_resolver.cpp
    # core/grip/ — 夹点
    core/grip/editor_grip_controller.cpp
    core/grip/editor_grip_provider.cpp
    core/grip/grip_marker_builder.cpp
    # core/session/ — 编辑会话总控
    core/session/editor_session.cpp
    core/session/editor_overlay_service.cpp
    core/session/editor_preview_controller.cpp
    # document/ — 纯逻辑文档桥接层
    document/document_session.cpp
    document/document_render_cache.cpp
    document/document_render_binding.cpp
    document/document_pick_bridge.cpp
    document/document_selection_bridge.cpp
    document/document_view.cpp
    document/document_view_binding.cpp
)

target_include_directories(editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

mulan_copy_public_headers(editor editor
    command/command.h
    command/command_manager.h
    command/builtin_commands.h
    document/document_session.h
    document/document_change.h
    document/document_view.h
)

target_link_libraries(editor
    PUBLIC
        core
        io
    PRIVATE
        mulan::profiling
        view
        render
        interaction
        asset
        scene
        modeling_core
)

target_precompile_headers(editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/pch.h
)

set_target_properties(editor PROPERTIES OUTPUT_NAME mulan_editor)

target_compile_definitions(editor PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
