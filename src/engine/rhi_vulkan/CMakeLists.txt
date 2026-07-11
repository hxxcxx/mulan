# rhi_vulkan - Vulkan RHI 后端。
# 全平台可用，通过 AutoRegisterDeviceBackend 编译期自注册到 DeviceFactory。
add_library(rhi_vulkan STATIC
    backend_register.cpp
    vk_buffer.cpp
    vk_bind_group.cpp
    vk_compute_pipeline.cpp
    vk_texture.cpp
    vk_shader.cpp
    vk_fence.cpp
    vk_frame_context.cpp
    vk_frame_scheduler.cpp
    vk_command_list.cpp
    vk_pipeline_state.cpp
    vk_descriptor_allocator.cpp
    vk_upload_context.cpp
    vk_swap_chain.cpp
    vk_render_target.cpp
    vk_resource_factory.cpp
    vk_device.cpp
    vk_device_init.cpp
    vk_sampler.cpp
)

mulan_copy_public_headers(rhi_vulkan rhi_vulkan)

target_link_libraries(rhi_vulkan
    PUBLIC
        rhi
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
)

target_precompile_headers(rhi_vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(rhi_vulkan PROPERTIES OUTPUT_NAME mulan_rhi_vulkan)

target_compile_definitions(rhi_vulkan PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

if(MSVC)
    target_compile_options(rhi_vulkan PRIVATE /FS)
endif()
