# rhi_dx11 - Windows D3D11 RHI backend.
add_library(rhi_dx11 STATIC
    backend_register.cpp
    dx11_bind_group.cpp
    dx11_buffer.cpp
    dx11_command_list.cpp
    dx11_transient_uniform_arena.cpp
    dx11_device.cpp
    dx11_fence.cpp
    dx11_pipeline_state.cpp
    dx11_render_target.cpp
    dx11_sampler.cpp
    dx11_shader.cpp
    dx11_swap_chain.cpp
    dx11_texture.cpp
)

target_link_libraries(rhi_dx11 PRIVATE rhi mulan::profiling d3d11 dxgi)
mulan_copy_public_headers(rhi_dx11 rhi_dx11 backend.h)
target_precompile_headers(rhi_dx11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h)
set_target_properties(rhi_dx11 PROPERTIES OUTPUT_NAME mulan_rhi_dx11)
target_compile_definitions(rhi_dx11 PRIVATE $<$<CONFIG:Debug>:_DEBUG> _USE_MATH_DEFINES)

if(MSVC)
    target_compile_options(rhi_dx11 PRIVATE /FS)
endif()
