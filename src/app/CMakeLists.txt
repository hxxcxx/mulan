# Application executable and Qt UI layer.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(MULAN_APP_SOURCES
    main.cpp
    command/command_manager.cpp
    command/builtin_commands.cpp
    editor/document_operation.cpp
    editor/document_operation_executor.cpp
    editor/control_polygon_builder.cpp
    editor/draft_geometry.cpp
    editor/editor_action.cpp
    editor/editor_preview_controller.cpp
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
    ui/document_render_cache.cpp
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

set(MULAN_OCCT_3RDPARTY_RUNTIME_DIRS)
if(OCCT_3RDPARTY_DIR)
    mulan_collect_runtime_bin_dirs(MULAN_OCCT_3RDPARTY_RUNTIME_DIRS "${OCCT_3RDPARTY_DIR}")
endif()

set(MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES)
if(MULAN_ENABLE_DEV_RUNTIME_COPY OR MULAN_ENABLE_POST_BUILD_DEPLOY OR MULAN_ENABLE_RUNTIME_DEPLOY_TARGET)
    mulan_find_named_dlls(MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES
        NAMES ${MULAN_OCCT_3RDPARTY_DLL_NAMES}
        SEARCH_DIRS ${MULAN_OCCT_3RDPARTY_RUNTIME_DIRS}
    )
endif()

set(MULAN_RUNTIME_DLL_FILES
    ${OCCT_RUNTIME_DLL_FILES}
    ${MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES}
)
list(REMOVE_DUPLICATES MULAN_RUNTIME_DLL_FILES)

set(MULAN_QT_ROOT_DIR)
set(MULAN_QT_BIN_DIR)
if(TARGET Qt6::qmake)
    get_target_property(MULAN_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    if(MULAN_QMAKE_EXECUTABLE)
        get_filename_component(MULAN_QT_BIN_DIR "${MULAN_QMAKE_EXECUTABLE}" DIRECTORY)
        get_filename_component(MULAN_QT_ROOT_DIR "${MULAN_QT_BIN_DIR}" DIRECTORY)
    endif()
endif()
if(NOT MULAN_QT_ROOT_DIR AND Qt6_DIR)
    get_filename_component(MULAN_QT_ROOT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    set(MULAN_QT_BIN_DIR "${MULAN_QT_ROOT_DIR}/bin")
endif()
set(MULAN_QT_PLUGIN_DIR "${MULAN_QT_ROOT_DIR}/plugins")
set(MULAN_QT_PLATFORM_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}/platforms")

set(MULAN_QT_PLATFORM_PLUGIN_DLLS)
foreach(platform_plugin_name qwindows.dll qwindowsd.dll)
    set(platform_plugin "${MULAN_QT_PLATFORM_PLUGIN_DIR}/${platform_plugin_name}")
    if(EXISTS "${platform_plugin}")
        list(APPEND MULAN_QT_PLATFORM_PLUGIN_DLLS "${platform_plugin}")
    endif()
endforeach()

if(TARGET Qt6::windeployqt)
    set(MULAN_WINDEPLOYQT_EXECUTABLE "$<TARGET_FILE:Qt6::windeployqt>")
elseif(MULAN_QT_BIN_DIR)
    find_program(MULAN_WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${MULAN_QT_BIN_DIR}"
        NO_DEFAULT_PATH
    )
endif()

if(MULAN_ENABLE_DEV_RUNTIME_ENV)
    set(MULAN_DEV_RUNTIME_PATHS
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        "${MULAN_QT_BIN_DIR}"
        "${OCCT_BIN_DIR}"
        ${MULAN_OCCT_3RDPARTY_RUNTIME_DIRS}
    )
    mulan_configure_windows_debug_runtime(${PROJECT_NAME}
        PATHS ${MULAN_DEV_RUNTIME_PATHS}
        QT_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}"
        QT_PLATFORM_PLUGIN_DIR "${MULAN_QT_PLATFORM_PLUGIN_DIR}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

if(MULAN_ENABLE_DEV_RUNTIME_COPY)
    mulan_copy_target_runtime_dlls_post_build(${PROJECT_NAME})
    mulan_copy_files_post_build(${PROJECT_NAME}
        FILES ${MULAN_RUNTIME_DLL_FILES}
    )

    foreach(platform_plugin IN LISTS MULAN_QT_PLATFORM_PLUGIN_DLLS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${platform_plugin}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
            COMMENT "Syncing Qt platform plugin..."
        )
    endforeach()
endif()

if(MULAN_ENABLE_POST_BUILD_DEPLOY)
    if(NOT MULAN_ENABLE_DEV_RUNTIME_COPY)
        mulan_copy_files_post_build(${PROJECT_NAME}
            FILES ${MULAN_RUNTIME_DLL_FILES}
        )
    endif()

    if(MULAN_DEPLOY_QT_WITH_WINDEPLOYQT AND MULAN_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${MULAN_WINDEPLOYQT_EXECUTABLE}"
                --no-translations --no-opengl-sw --no-system-d3d-compiler
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Deploying Qt runtime via windeployqt..."
        )
    endif()
endif()

if(MULAN_ENABLE_RUNTIME_DEPLOY_TARGET)
    set(MULAN_RUNTIME_DEPLOY_DEPENDS)

    mulan_add_copy_files_target(${PROJECT_NAME}_deploy_runtime_files
        DESTINATION "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        DEPENDS ${PROJECT_NAME}
        FILES ${MULAN_RUNTIME_DLL_FILES}
        COMMENT "Deploying OCCT runtime DLLs..."
    )
    list(APPEND MULAN_RUNTIME_DEPLOY_DEPENDS ${PROJECT_NAME}_deploy_runtime_files)

    if(MULAN_DEPLOY_QT_WITH_WINDEPLOYQT AND MULAN_WINDEPLOYQT_EXECUTABLE)
        add_custom_target(${PROJECT_NAME}_deploy_qt
            COMMAND "${MULAN_WINDEPLOYQT_EXECUTABLE}"
                --no-translations --no-opengl-sw --no-system-d3d-compiler
                "$<TARGET_FILE:${PROJECT_NAME}>"
            DEPENDS ${PROJECT_NAME}
            COMMENT "Deploying Qt runtime via windeployqt..."
        )
        list(APPEND MULAN_RUNTIME_DEPLOY_DEPENDS ${PROJECT_NAME}_deploy_qt)
    endif()

    add_custom_target(${PROJECT_NAME}_deploy_runtime
        DEPENDS ${MULAN_RUNTIME_DEPLOY_DEPENDS}
    )
endif()
