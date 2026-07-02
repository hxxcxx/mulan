# APP - 主应用程序
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(${PROJECT_NAME}
    main.cpp
    resource/icon.qrc
    ui/ui_document.h
    ui/ui_document.cpp
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
    World
    Engine
    RenderScene
    Command
    $<LINK_LIBRARY:WHOLE_ARCHIVE,IO>
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

# ── Post-build: copy OCCT DLLs ──
if(DEFINED OCCT_BIN_DIR AND EXISTS "${OCCT_BIN_DIR}")
    file(GLOB OCCT_DLLS "${OCCT_BIN_DIR}/*.dll")
    foreach(dll ${OCCT_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()

# ── Post-build: copy OCCT 3rdparty DLLs ──
if(DEFINED OCCT_3RDPARTY_DIR AND EXISTS "${OCCT_3RDPARTY_DIR}")
    file(GLOB_RECURSE OCCT_3RDPARTY_DLLS "${OCCT_3RDPARTY_DIR}/*.dll")
    foreach(dll ${OCCT_3RDPARTY_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()

# ── Post-build: deploy Qt DLLs via windeployqt ──
if(Qt6_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
            --no-translations --no-opengl-sw --no-system-d3d-compiler
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt DLLs via windeployqt..."
    )
endif()
