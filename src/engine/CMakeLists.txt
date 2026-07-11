# Engine - 渲染引擎容器目录，包含以下独立 target：
#   rhi          硬件抽象层（内部模块，零后端依赖）
#   rhi_vulkan   Vulkan 后端（全平台，CMake option 控制）
#   rhi_dx12     DX12 后端（Windows only，CMake option 控制）
#   render       渲染管线（对外暴露）
#   platform     平台/窗口层（对外暴露）
#   interaction  交互层（对外暴露）

option(MULAN_ENABLE_VULKAN_BACKEND "Build Vulkan RHI backend" ON)
option(MULAN_ENABLE_D3D12_BACKEND "Build DX12 RHI backend (Windows only)" ${WIN32})

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_subdirectory(rhi)

if(MULAN_ENABLE_VULKAN_BACKEND)
    add_subdirectory(rhi_vulkan)
endif()

if(MULAN_ENABLE_D3D12_BACKEND AND WIN32)
    add_subdirectory(rhi_dx12)
endif()

add_subdirectory(render)
add_subdirectory(platform)
add_subdirectory(interaction)
