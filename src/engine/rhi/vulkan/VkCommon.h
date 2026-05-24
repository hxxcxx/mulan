/**
 * @file VkCommon.h
 * @brief Vulkan后端预编译头，共享头文件包含与通用工具
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1

#include <vulkan/vulkan.hpp>


#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <cstdio>
