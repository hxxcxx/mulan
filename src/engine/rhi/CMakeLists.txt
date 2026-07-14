# rhi - 硬件抽象层：抽象接口 + 后端注册表。
# 零后端依赖 —— 各后端编译期自注册到 DeviceFactory，rhi 抽象层不含任何后端头文件。
add_library(rhi STATIC
    resource.cpp
    command_list.cpp
    device.cpp
    device_factory.cpp
    bind_group.cpp
    bind_group_layout.cpp
    pipeline_validation.cpp
    transient_uniform_allocator.cpp
)

mulan_copy_public_headers(rhi rhi)

target_link_libraries(rhi
    PUBLIC
        core
        math
        graphics
)

target_precompile_headers(rhi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(rhi PROPERTIES OUTPUT_NAME mulan_rhi)

target_compile_definitions(rhi PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

if(MSVC)
    target_compile_options(rhi PRIVATE /FS)
endif()
