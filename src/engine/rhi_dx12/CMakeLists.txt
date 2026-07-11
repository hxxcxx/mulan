# rhi_dx12 - DX12 RHI 后端（Windows only）。
# 通过 AutoRegisterDeviceBackend 编译期自注册到 DeviceFactory。
add_library(rhi_dx12 STATIC
    backend_register.cpp
    dx12_fence.cpp
    dx12_bind_group.cpp
    dx12_descriptor_allocator.cpp
    dx12_buffer.cpp
    dx12_upload_context.cpp
    dx12_texture.cpp
    dx12_shader.cpp
    dx12_pipeline_state.cpp
    dx12_command_list.cpp
    dx12_swap_chain.cpp
    dx12_render_target.cpp
    dx12_frame_context.cpp
    dx12_device.cpp
    dx12_sampler.cpp
)

mulan_copy_public_headers(rhi_dx12 rhi_dx12)

target_link_libraries(rhi_dx12
    PUBLIC
        rhi
    PRIVATE
        d3d12
        dxgi
)

target_precompile_headers(rhi_dx12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(rhi_dx12 PROPERTIES OUTPUT_NAME mulan_rhi_dx12)

target_compile_definitions(rhi_dx12 PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

if(MSVC)
    target_compile_options(rhi_dx12 PRIVATE /FS)
endif()
