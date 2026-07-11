# platform - 平台层：原生窗口实现。
# 对外暴露模块，依赖 rhi 获取 window.h 契约。
add_library(platform STATIC
    win32_window.cpp
)

mulan_copy_public_headers(platform platform)

target_link_libraries(platform
    PUBLIC
        rhi
        core
)

target_precompile_headers(platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(platform PROPERTIES OUTPUT_NAME mulan_platform)

target_compile_definitions(platform PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(platform PRIVATE /FS)
endif()
