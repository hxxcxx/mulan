# Application executable and Qt UI layer.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(MULAN_APP_SOURCES
    main.cpp
    command/command_manager.cpp
    command/builtin_commands.cpp
    editor/document_operation.cpp
    editor/control_polygon_builder.cpp
    editor/draft_geometry.cpp
    editor/editor_action.cpp
    editor/editor_input_resolver.cpp
    editor/editor_grip_provider.cpp
    editor/editor_scene_snap_provider.cpp
    editor/editor_selection.cpp
    editor/editor_snap_collector.cpp
    editor/editor_snap_resolver.cpp
    editor/editor_session.cpp
    editor/point_drawing_tool.cpp
    editor/tool_controller.cpp
    editor/param_curve_tool.cpp
    editor/polyline_tool.cpp
    editor/circle_tool.cpp
    editor/face_tool.cpp
    editor/line_tool.cpp
    editor/grip_drag_tool.cpp
    editor/grip_marker_builder.cpp
    editor/snap_marker_builder.cpp
    ui/document_session.cpp
    ui/document_view.cpp
    ui/document_view_binding.cpp
    ui/document_area.cpp
    ui/main_window.cpp
    ui/doc_widget.cpp
    ui/engine_settings.cpp
    ui/engine_settings_dialog.cpp
)

set(MULAN_APP_QT_HEADERS
    ui/document_area.h
    ui/main_window.h
    ui/doc_widget.h
    ui/engine_settings_dialog.h
)

add_executable(${PROJECT_NAME}
    ${MULAN_APP_SOURCES}
    ${MULAN_APP_QT_HEADERS}
    resource/icon.qrc
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    view
    engine
    $<LINK_LIBRARY:WHOLE_ARCHIVE,io>
    SARibbon
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_DIR="${MULAN_SHADER_OUTPUT_DIR}"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

mulan_copy_dlls_post_build(${PROJECT_NAME} "${OCCT_BIN_DIR}")
mulan_copy_dlls_post_build(${PROJECT_NAME} "${OCCT_3RDPARTY_DIR}" RECURSE)

if(TARGET Qt6::windeployqt)
    set(MULAN_WINDEPLOYQT_EXECUTABLE "$<TARGET_FILE:Qt6::windeployqt>")
elseif(Qt6_DIR)
    find_program(MULAN_WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
        NO_DEFAULT_PATH
    )
endif()

if(MULAN_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${MULAN_WINDEPLOYQT_EXECUTABLE}"
            --no-translations --no-opengl-sw --no-system-d3d-compiler
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt DLLs via windeployqt..."
    )
endif()
