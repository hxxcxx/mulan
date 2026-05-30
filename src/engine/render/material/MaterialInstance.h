/**
 * @file MaterialInstance.h
 * @brief 材质实例 — 基于模板创建，可独立修改参数
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "Material.h"

#include <memory>
#include <string>
#include <functional>

namespace mulan::engine {

// ============================================================
// 材质实例 — 基于 MaterialAsset 模板创建
//
// 与模板区别：
//  - 模板（MaterialAsset）是静态的、共享的
//  - 实例（MaterialInstance）是动态的、独立的
//  - 修改实例不影响模板和其他实例
//  - 实例可持有运行时计算的纹理索引
//
// 支持属性覆盖：
//  - 每个属性有独立的 override 标志
//  - 未覆盖的属性从模板取值
//  - resetToTemplate() 清除所有覆盖
// ============================================================

class MaterialAsset;  // 前向声明

/// 属性覆盖标志位
enum class MaterialOverrideFlags : uint32_t {
    None            = 0,
    BaseColor       = 1u << 0,
    Alpha           = 1u << 1,
    Metallic        = 1u << 2,
    Roughness       = 1u << 3,
    AO              = 1u << 4,
    Specular        = 1u << 5,
    Shininess       = 1u << 6,
    Emissive        = 1u << 7,
    EmissiveStrength = 1u << 8,
    AlphaCutoff     = 1u << 9,
    DoubleSided     = 1u << 10,
    MaterialType    = 1u << 11,
    AlphaMode       = 1u << 12,
    Textures        = 1u << 13,
    All             = ~0u,
};

inline MaterialOverrideFlags operator|(MaterialOverrideFlags a, MaterialOverrideFlags b) {
    return static_cast<MaterialOverrideFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline MaterialOverrideFlags operator&(MaterialOverrideFlags a, MaterialOverrideFlags b) {
    return static_cast<MaterialOverrideFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasFlag(MaterialOverrideFlags flags, MaterialOverrideFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}
inline void setFlag(MaterialOverrideFlags& flags, MaterialOverrideFlags flag) {
    flags = flags | flag;
}

class MaterialInstance {
public:
    /// 从模板资产创建实例
    explicit MaterialInstance(const MaterialAsset* asset);

    /// 从 Material 创建实例（无模板）
    explicit MaterialInstance(Material material);

    ~MaterialInstance() = default;

    // 禁用拷贝（每个实例独立）
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;

    // 支持移动
    MaterialInstance(MaterialInstance&&) noexcept = default;
    MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

    // --- 基础参数修改 ---

    void setBaseColor(const Vec3& color);
    void setMetallic(double metallic);
    void setRoughness(double roughness);
    void setAlpha(double alpha);
    void setEmissive(const Vec3& color, double strength = 1.0);
    void setDoubleSided(bool doubleSided);
    void setAlphaCutoff(double cutoff);
    void setSpecular(const Vec3& color);
    void setShininess(double shininess);
    void setAO(double ao);

    // --- 类型/模式修改 ---
    void setMaterialType(MaterialType type);
    void setAlphaMode(AlphaMode mode);

    // --- 纹理设置 ---
    // 纹理通过 TextureCache 加载后，用索引关联
    void setAlbedoTexture(uint16_t textureId);
    void setNormalTexture(uint16_t textureId);
    void setMetallicRoughnessTexture(uint16_t textureId);
    void setEmissiveTexture(uint16_t textureId);
    void setAoTexture(uint16_t textureId);
    void setTextureBySlot(TextureSlot slot, uint16_t textureId);

    /// 设置纹理路径（用于序列化/延迟加载）
    void setTexturePath(TextureSlot slot, const std::string& path);

    // --- 便捷参数 ---

    void setColor(const Vec3& color) { setBaseColor(color); }

    /// 应用高亮色（用于选中状态）
    void setHighlight(const Vec3& highlightColor);

    /// 清除高亮，恢复覆盖前的状态
    void clearHighlight();

    /// 恢复默认参数（从模板重新初始化）
    void resetToTemplate();

    /// 清除所有属性覆盖，恢复为模板值
    void resetOverrides();

    // --- 查询 ---

    /// 获取解析后的材质参数（覆盖 + 模板合并）
    const Material& material() const { return m_material; }
    Material&       material()       { return m_material; }

    /// 原始覆盖材质（不含模板合并）
    const Material& overrides() const { return m_overrides; }

    uint32_t templateId()   const { return m_templateId; }
    bool    isFromTemplate() const { return m_templateId != 0; }
    bool    isDirty()        const { return m_dirty; }
    MaterialOverrideFlags overrideFlags() const { return m_overrideFlags; }

    /// 标记为已上传（内部使用）
    void markClean() { m_dirty = false; }

    /// 标记为需要重新上传
    void markDirty() { m_dirty = true; }

    /// 获取 GPU 数据
    MaterialGPU toGPU() const { return MaterialGPU::fromMaterial(m_material); }

    /// 是否有某个属性被覆盖
    bool isOverridden(MaterialOverrideFlags flag) const {
        return hasFlag(m_overrideFlags, flag);
    }

private:
    void rebuildFromTemplate();

    Material             m_material;       // 最终解析后的参数（覆盖 + 模板合并）
    Material             m_overrides;      // 仅覆盖值（其他字段默认值）
    MaterialOverrideFlags m_overrideFlags = MaterialOverrideFlags::None;
    uint32_t             m_templateId = 0; // 模板 ID（0 表示无模板）
    bool                 m_dirty = true;   // 是否需要重新上传 UBO

    // 保存高亮前的状态用于 clearHighlight
    bool                 m_highlighted = false;
    Material             m_savedMaterial;  // 高亮前保存
};

} // namespace mulan::engine
