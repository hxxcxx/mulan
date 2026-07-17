/**
 * @file profile.h
 * @brief 项目性能分析薄适配接口。
 *
 * 业务模块只依赖 MULAN_PROFILE_* 宏。启用性能分析时，宏直接映射到 Tracy
 * 官方内联接口；关闭时全部编译为空操作，不引入运行时调用或链接依赖。
 *
 * @author hxxcxx
 * @date 2026-07-17
 */

#pragma once

#if defined(MULAN_ENABLE_PROFILING) && MULAN_ENABLE_PROFILING

#include <tracy/Tracy.hpp>

#define MULAN_PROFILE_ZONE()       ZoneScoped
#define MULAN_PROFILE_ZONE_N(name) ZoneScopedN(name)
#define MULAN_PROFILE_FRAME()      FrameMark
#define MULAN_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define MULAN_PROFILE_ZONE()       ((void) 0)
#define MULAN_PROFILE_ZONE_N(name) ((void) 0)
#define MULAN_PROFILE_FRAME()      ((void) 0)
#define MULAN_PROFILE_THREAD(name) ((void) 0)

#endif
