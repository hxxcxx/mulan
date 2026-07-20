# rhi_vulkan - Vulkan RHI 后端。
# 全平台可用，导出 BackendModule，由最终应用显式注册。
set(MULAN_VULKAN_PLATFORM_SOURCE)
if(WIN32)
    set(MULAN_VULKAN_PLATFORM_SOURCE platform/vk_platform_win32.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MULAN_VULKAN_PLATFORM_SOURCE platform/vk_platform_x11.cpp)
else()
    message(FATAL_ERROR "The Vulkan backend has no platform implementation for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(rhi_vulkan STATIC
    backend_register.cpp
    ${MULAN_VULKAN_PLATFORM_SOURCE}
    vk_buffer.cpp
    vk_transient_uniform_arena.cpp
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

mulan_copy_public_headers(rhi_vulkan rhi_vulkan backend.h)

target_link_libraries(rhi_vulkan
    PRIVATE
        rhi
        mulan::profiling
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(MULAN_XCB_INCLUDE_DIR NAMES xcb/xcb.h REQUIRED)
    target_include_directories(rhi_vulkan PRIVATE "${MULAN_XCB_INCLUDE_DIR}")
    target_link_libraries(rhi_vulkan PRIVATE ${CMAKE_DL_LIBS})
endif()

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
