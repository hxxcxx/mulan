# Application executable: Qt UI 壳。编辑逻辑在 editor STATIC target 中。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(MULAN_APP_SOURCES
    main.cpp
    bootstrap/rhi_backends.cpp
    services/document_open_service.cpp
    services/document_open_service.h
    services/recent_files_store.cpp
    services/recent_files_store.h
    services/recent_thumbnail_service.cpp
    services/recent_thumbnail_service.h
    ui/document_page.cpp
    ui/document_workspace.cpp
    ui/startup_page.cpp
    ui/main_window.cpp
    ui/document_viewport.cpp
    ui/qt_viewport_input_adapter.cpp
    ui/engine_settings.cpp
    ui/engine_settings_dialog.cpp
    ui/profiler_window.cpp
)

set(MULAN_APP_QT_HEADERS
    ui/document_page.h
    ui/document_workspace.h
    ui/startup_page.h
    ui/main_window.h
    ui/document_viewport.h
    ui/engine_settings_dialog.h
    ui/profiler_window.h
)

if(WIN32)
    list(APPEND MULAN_APP_SOURCES
        platform/qt_native_window_handle_win32.cpp
        resource/app-icon.rc
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND MULAN_APP_SOURCES platform/qt_native_window_handle_x11.cpp)
else()
    message(FATAL_ERROR "The Qt application has no native-window adapter for ${CMAKE_SYSTEM_NAME}")
endif()

add_executable(${PROJECT_NAME}
    ${MULAN_APP_SOURCES}
    ${MULAN_APP_QT_HEADERS}
    resource/icon.qrc
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    mulan::profiling
    editor
    interaction
    io
    rhi
    view
    runtime
    $<$<TARGET_EXISTS:rhi_vulkan>:rhi_vulkan>
    $<$<TARGET_EXISTS:rhi_dx12>:rhi_dx12>
    $<$<TARGET_EXISTS:rhi_dx11>:rhi_dx11>
    $<$<TARGET_EXISTS:rhi_opengl>:rhi_opengl>
    SARibbon
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::Widgets
)

# 建模后端是运行时插件，不参与 app 链接；但可执行程序的开发构建必须同步产出启用的插件。
if(TARGET modeling_occt)
    add_dependencies(${PROJECT_NAME} modeling_occt)
endif()
if(TARGET modeling_truck)
    add_dependencies(${PROJECT_NAME} modeling_truck)
endif()

if(WIN32)
    # Windows 开发目录可能残留上一次启用的插件；禁用后清理固定名称，避免 runtime 误加载。
    if(NOT MULAN_ENABLE_TRUCK_BACKEND)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/mulan_modeling_truck.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/mulan_modeling_truck.pdb"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/truck_bridge.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/backends/mulan_modeling_truck.dll"
            COMMENT "Removing disabled truck backend runtime artifacts..."
        )
    endif()

    if(NOT MULAN_ENABLE_OCCT_BACKEND)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/mulan_modeling_occt.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/mulan_modeling_occt.pdb"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/backends/mulan_modeling_occt.dll"
            COMMENT "Removing disabled OCCT backend runtime artifacts..."
        )
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_DIR="${MULAN_SHADER_OUTPUT_DIR}"
    MULAN_HAS_RHI_VULKAN=$<BOOL:$<TARGET_EXISTS:rhi_vulkan>>
    MULAN_HAS_RHI_D3D12=$<BOOL:$<TARGET_EXISTS:rhi_dx12>>
    MULAN_HAS_RHI_D3D11=$<BOOL:$<TARGET_EXISTS:rhi_dx11>>
    MULAN_HAS_RHI_OPENGL=$<BOOL:$<TARGET_EXISTS:rhi_opengl>>
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

if(NOT WIN32)
    # Linux M1 直接从构建树运行；安装布局与运行时部署在后续里程碑实现。
    return()
endif()

set(MULAN_OCCT_3RDPARTY_RUNTIME_DIRS)
if(OCCT_3RDPARTY_DIR)
    mulan_collect_runtime_bin_dirs(MULAN_OCCT_3RDPARTY_RUNTIME_DIRS "${OCCT_3RDPARTY_DIR}")
endif()

set(MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES)
mulan_find_named_dlls(MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES
    NAMES ${MULAN_OCCT_3RDPARTY_DLL_NAMES}
    SEARCH_DIRS ${MULAN_OCCT_3RDPARTY_RUNTIME_DIRS}
)

set(MULAN_RUNTIME_DLL_FILES
    ${OCCT_RUNTIME_DLL_FILES}
    ${MULAN_OCCT_3RDPARTY_RUNTIME_DLL_FILES}
)
list(REMOVE_DUPLICATES MULAN_RUNTIME_DLL_FILES)

set(MULAN_QT_ROOT_DIR)
set(MULAN_QT_BIN_DIR)
set(MULAN_QT_PLUGIN_DIR)
if(QT6_INSTALL_PREFIX)
    set(MULAN_QT_ROOT_DIR "${QT6_INSTALL_PREFIX}")
    set(MULAN_QT_BIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    set(MULAN_QT_PLUGIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
endif()
set(MULAN_QT_ICONENGINES_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}/iconengines")
set(MULAN_QT_IMAGEFORMATS_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}/imageformats")
set(MULAN_QT_PLATFORM_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}/platforms")

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
        "${MULAN_QT_BIN_DIR}"
        "${OCCT_BIN_DIR}"
        ${MULAN_OCCT_3RDPARTY_RUNTIME_DIRS}
    )
    mulan_configure_windows_debug_runtime(${PROJECT_NAME}
        PATHS ${MULAN_DEV_RUNTIME_PATHS}
        QT_PLUGIN_DIR "${MULAN_QT_PLUGIN_DIR}"
        QT_PLATFORM_PLUGIN_DIR "${MULAN_QT_PLATFORM_PLUGIN_DIR}"
    )
endif()

# 开发运行时只补齐链接闭包之外的文件：
# - 普通链接 DLL 由 vcpkg app-local 在目标构建后自动同步；
# - 此处只复制 OCCT/第三方运行库及应用实际需要的 Qt 插件；
# - windeployqt 保留给显式发布流程，不进入日常 F5 路径。
set(MULAN_DEV_RUNTIME_COMMANDS)
foreach(runtime_dll IN LISTS MULAN_RUNTIME_DLL_FILES)
    list(APPEND MULAN_DEV_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${runtime_dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

# qsvg/qsvgicon 是动态插件，插件对 Qt6Svg 的依赖不会进入 app 的链接闭包，需随插件显式同步。
if(TARGET Qt6::Svg)
    list(APPEND MULAN_DEV_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Svg>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

if(TARGET Qt6::QWindowsIntegrationPlugin)
    list(APPEND MULAN_DEV_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
    )
endif()

if(TARGET Qt6::QSvgPlugin)
    list(APPEND MULAN_DEV_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QSvgPlugin>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"
    )
endif()

if(TARGET Qt6::QSvgIconPlugin)
    list(APPEND MULAN_DEV_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/iconengines"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QSvgIconPlugin>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/iconengines"
    )
endif()

add_custom_target(${PROJECT_NAME}_dev_runtime
    ${MULAN_DEV_RUNTIME_COMMANDS}
    DEPENDS ${PROJECT_NAME}
    COMMENT "Preparing the minimal development runtime..."
    VERBATIM
)

if(MULAN_ENABLE_POST_BUILD_DEPLOY AND MULAN_DEV_RUNTIME_COMMANDS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        ${MULAN_DEV_RUNTIME_COMMANDS}
        COMMENT "Preparing the minimal development runtime..."
        VERBATIM
    )
endif()

if(MULAN_ENABLE_RUNTIME_DEPLOY_TARGET)
    set(MULAN_RUNTIME_DEPLOY_DEPENDS)

    if(TARGET modeling_occt)
        list(APPEND MULAN_RUNTIME_DEPLOY_DEPENDS modeling_occt)
    endif()
    if(TARGET modeling_truck)
        list(APPEND MULAN_RUNTIME_DEPLOY_DEPENDS modeling_truck)
    endif()

    mulan_add_copy_files_target(${PROJECT_NAME}_deploy_runtime_files
        DESTINATION "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        DEPENDS ${PROJECT_NAME}
        FILES
            $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
            ${MULAN_RUNTIME_DLL_FILES}
        COMMENT "Deploying linked and OCCT runtime DLLs..."
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
