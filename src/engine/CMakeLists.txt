# Engine - rendering backend, RHI, camera, interaction, and render stages.
add_library(engine STATIC
    platform/win32_window.cpp
    interaction/work_plane.cpp
    rhi/resource.cpp
    rhi/device.cpp
    rhi/device_factory.cpp
    rhi/bind_group_layout.cpp
    render/camera/camera.cpp
    render/camera/turntable_rotation.cpp
    render/camera/trackball_rotation.cpp
    render/asset_gpu_registry.cpp
    render/environment_map.cpp
    render/mesh_draw_command.cpp
    render/technique/render_technique.cpp
    # Render frontend / backend
    render/frontend/render_contract.cpp
    render/frontend/render_world.cpp
    render/frontend/render_workload.cpp
    render/backend/render_compiler.cpp
    render/backend/render_renderer.cpp
    # Draw executors / stages
    render/draw/geometry_draw_shared_resources.cpp
    render/draw/geometry_draw_executor.cpp
    render/forward/face_stage.cpp
    render/forward/edge_stage.cpp
    # Text rendering (MSDF)
    render/text/font_atlas.cpp
    render/text/text_layout.cpp
    render/text/font_manager.cpp
    render/text/text_stage.cpp
    # Overlay / navigation stages
    render/overlay/view_cube_stage.cpp
    # Material system
    render/material/material_cache.cpp
    # Vulkan backend
    rhi/vulkan/vk_buffer.cpp
    rhi/vulkan/vk_bind_group.cpp
    rhi/vulkan/vk_compute_pipeline.cpp
    rhi/vulkan/vk_texture.cpp
    rhi/vulkan/vk_shader.cpp
    rhi/vulkan/vk_fence.cpp
    rhi/vulkan/vk_frame_context.cpp
    rhi/vulkan/vk_frame_scheduler.cpp
    rhi/vulkan/vk_command_list.cpp
    rhi/vulkan/vk_pipeline_state.cpp
    rhi/vulkan/vk_descriptor_allocator.cpp
    rhi/vulkan/vk_upload_context.cpp
    rhi/vulkan/vk_swap_chain.cpp
    rhi/vulkan/vk_render_target.cpp
    rhi/vulkan/vk_resource_factory.cpp
    rhi/vulkan/vk_device.cpp
    rhi/vulkan/vk_device_init.cpp
    rhi/vulkan/vk_sampler.cpp
    # D3D12 backend
    rhi/d3d12/dx12_fence.cpp
    rhi/d3d12/dx12_bind_group.cpp
    rhi/d3d12/dx12_descriptor_allocator.cpp
    rhi/d3d12/dx12_buffer.cpp
    rhi/d3d12/dx12_upload_context.cpp
    rhi/d3d12/dx12_texture.cpp
    rhi/d3d12/dx12_shader.cpp
    rhi/d3d12/dx12_pipeline_state.cpp
    rhi/d3d12/dx12_command_list.cpp
    rhi/d3d12/dx12_swap_chain.cpp
    rhi/d3d12/dx12_render_target.cpp
    rhi/d3d12/dx12_frame_context.cpp
    rhi/d3d12/dx12_device.cpp
    rhi/d3d12/dx12_sampler.cpp
)

mulan_copy_public_headers(engine engine)

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(engine PUBLIC
    core
    math
    graphics
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    nlohmann_json::nlohmann_json
    msdfgen::msdfgen
    msdf-atlas-gen
    d3d12
    dxgi
)

target_compile_definitions(engine PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

target_precompile_headers(engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(engine PROPERTIES OUTPUT_NAME mulan_engine)

if(MSVC)
    target_compile_options(engine PRIVATE /FS)
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${MULAN_SHADER_OUTPUT_DIR}")
set(SHADER_SOURCES
    solid.vert.hlsl
    solid.frag.hlsl
    albedo.vert.hlsl
    albedo.frag.hlsl
    pbr.vert.hlsl
    pbr.frag.hlsl
    pbr_tangent.vert.hlsl
    pbr_tangent.frag.hlsl
    wireframe.vert.hlsl
    wireframe.frag.hlsl
    edge.vert.hlsl
    edge.frag.hlsl
    viewcube.frag.hlsl
    viewcube_line.frag.hlsl
    text.vert.hlsl
    text.frag.hlsl
    ibl.vert.hlsl
    ibl_irradiance.frag.hlsl
    ibl_prefilter.frag.hlsl
    ibl_brdf_lut.frag.hlsl
)

mulan_add_hlsl_shaders(mulan_shaders
    SOURCE_DIR "${SHADER_SOURCE_DIR}"
    OUTPUT_DIR "${SHADER_OUTPUT_DIR}"
    SOURCES ${SHADER_SOURCES}
)
add_dependencies(engine mulan_shaders)

target_compile_definitions(engine PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
)
