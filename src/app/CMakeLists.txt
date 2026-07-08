# APP - 涓诲簲鐢ㄧ▼搴?
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(${PROJECT_NAME}
    main.cpp
    command/command.h
    command/command_manager.h
    command/command_manager.cpp
    command/builtin_commands.h
    command/builtin_commands.cpp
    editor/document_operation.h
    editor/document_operation.cpp
    editor/draft_geometry.h
    editor/draft_geometry.cpp
    editor/editor_action.h
    editor/editor_action.cpp
    editor/editor_input.h
    editor/editor_input_resolver.h
    editor/editor_input_resolver.cpp
    editor/editor_snap_resolver.h
    editor/editor_snap_resolver.cpp
    editor/editor_session.h
    editor/editor_session.cpp
    editor/editor_tool.h
    editor/tool_controller.h
    editor/tool_controller.cpp
    editor/line_tool.h
    editor/line_tool.cpp
    resource/icon.qrc
    ui/document_session.h
    ui/document_session.cpp
    ui/document_view.h
    ui/document_view.cpp
    ui/document_view_binding.h
    ui/document_view_binding.cpp
    ui/document_area.h
    ui/document_area.cpp
    ui/main_window.h
    ui/main_window.cpp
    ui/doc_widget.h
    ui/doc_widget.cpp
    ui/engine_settings.h
    ui/engine_settings.cpp
    ui/engine_settings_dialog.h
    ui/engine_settings_dialog.cpp
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
    SHADER_DIR="${CMAKE_SOURCE_DIR}/build/shaders"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

# 鈹€鈹€ Post-build: copy OCCT DLLs 鈹€鈹€
if(DEFINED OCCT_BIN_DIR AND EXISTS "${OCCT_BIN_DIR}")
    file(GLOB OCCT_DLLS "${OCCT_BIN_DIR}/*.dll")
    foreach(dll ${OCCT_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()

# 鈹€鈹€ Post-build: copy OCCT 3rdparty DLLs 鈹€鈹€
if(DEFINED OCCT_3RDPARTY_DIR AND EXISTS "${OCCT_3RDPARTY_DIR}")
    file(GLOB_RECURSE OCCT_3RDPARTY_DLLS "${OCCT_3RDPARTY_DIR}/*.dll")
    foreach(dll ${OCCT_3RDPARTY_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()

# 鈹€鈹€ Post-build: deploy Qt DLLs via windeployqt 鈹€鈹€
if(Qt6_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
            --no-translations --no-opengl-sw --no-system-d3d-compiler
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt DLLs via windeployqt..."
    )
endif()
