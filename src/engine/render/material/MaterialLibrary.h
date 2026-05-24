/**
 * @file MaterialLibrary.h
 * @brief 预设材质库 — 提供常用材质模板
 * @author hxxcxx
 * @date 2026-04-23
 *
 * MaterialLibrary 维护一个预设材质集合，支持：
 *  - 从内置预设加载
 *  - 从 JSON 文件加载自定义材质
 *  - 按名称/ID 查询预设
 *  - 注册自定义预设
 *
 * 使用方式：
 *   auto& lib = MaterialLibrary::instance();
 *   lib.init();  // 注册内置预设
 *   auto* mat = lib.find("Steel");
 *   if (mat) { ... }
 */

#pragma once

#include "Material.h"
#include "MaterialCache.h"
#include "MaterialSerializer.h"

#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// MaterialLibrary — 预设材质库
// ============================================================

class MaterialLibrary {
public:
    static MaterialLibrary& instance();

    /// 初始化：注册所有内置预设到 MaterialCache
    void init();

    /// 注册内置预设（金属、塑料、玻璃等）
    void registerBuiltinPresets();

    /// 从 JSON 文件加载自定义预设并注册到 MaterialCache
    /// @param path JSON 文件路径
    /// @return 加载的材质数量（0 = 失败）
    size_t loadPresetsFromFile(const std::string& path);

    /// 注册一个预设（同时加入 MaterialCache）
    uint32_t registerPreset(Material material);

    /// 按名称查找预设
    MaterialAsset* find(const std::string& name);

    /// 获取所有预设名称
    std::vector<std::string> presetNames() const;

    /// 预设数量
    size_t size() const { return m_presets.size(); }

    /// 清除所有预设（保留内置的）
    void clear();

private:
    MaterialLibrary() = default;
    ~MaterialLibrary() = default;

    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;

    // --- 内置预设工厂 ---
    static Material steel();
    static Material copper();
    static Material gold();
    static Material aluminum();
    static Material plastic_white();
    static Material plastic_red();
    static Material plastic_blue();
    static Material plastic_green();
    static Material rubber();
    static Material glass();
    static Material concrete();
    static Material wood();
    static Material marble();
    static Material ceramic();
    static Material emissive_white();
    static Material emissive_red();
    static Material emissive_blue();

    std::vector<std::string> m_presets;  // 已注册的预设名称列表
};

} // namespace mulan::Engine
