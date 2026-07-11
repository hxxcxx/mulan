# interaction - 交互层：相机操控、输入事件、工作平面、操作符。
# 对外暴露模块，依赖 render 获取 camera 类型。
add_library(interaction STATIC
    work_plane.cpp
)

mulan_copy_public_headers(interaction interaction)

target_link_libraries(interaction
    PUBLIC
        render
)

target_precompile_headers(interaction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(interaction PROPERTIES OUTPUT_NAME mulan_interaction)

target_compile_definitions(interaction PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
