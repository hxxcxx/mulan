/**
 * @file material_cache.h
 * @brief 材质缓存 — 管理 engine::Material 实例 + GPU UBO 偏移映射
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 重构后设计：
 *  - 直接存储 vector<Material>，无额外包装类（删掉了旧的 MaterialAsset 包装）
 *  - 用 vector index 作为材质句柄，offset = index × kMaterialSlotStride
 *  - 去单例化，由 Renderer 持有并通过引用注入
 */

#pragma once

#include "material.h"
#include "../../rhi/device.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <set>

namespace mulan::engine {

// ============================================================
// 材质句柄 — vector index，0 = 默认材质(DefaultPBR)
// ============================================================
using MaterialHandle = size_t;
static constexpr MaterialHandle kInvalidMaterialHandle = static_cast<size_t>(-1);

// ============================================================
// 材质缓存 — 非单例，由 Renderer 持有
// ============================================================

class MaterialCache {
public:
    /// 构造时注册默认材质(DefaultPBR/DefaultPhong/Wireframe)
    MaterialCache();
    ~MaterialCache() = default;

    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;

    // --- 材质注册 ---

    /// 注册一个新材质，返回句柄（vector index）
    MaterialHandle registerMaterial(Material material);

    /// 注册材质并指定名称（同名覆盖，句柄不变）
    MaterialHandle registerMaterial(const std::string& name, Material material);

    // --- 材质查找 ---

    /// 按句柄查找
    const Material* find(MaterialHandle handle) const;
    Material*       find(MaterialHandle handle);

    /// 按名称查找
    const Material* findByName(const std::string& name) const;
    Material*       findByName(const std::string& name);

    /// 获取所有材质（只读）
    const std::vector<Material>& all() const { return materials_; }

    /// 遍历所有材质
    void forEach(const std::function<void(const Material&)>& fn) const;

    // --- 材质修改 ---

    /// 修改材质参数（按句柄），返回是否成功
    bool updateMaterial(MaterialHandle handle, const Material& material);

    /// 修改材质参数（按名称），返回是否成功
    bool updateMaterial(const std::string& name, const Material& material);

    // --- 生命周期 ---

    /// 移除材质，返回是否成功
    bool remove(MaterialHandle handle);

    /// 清空所有材质（保留默认材质）
    void clear();

    /// 材质数量
    size_t size() const { return materials_.size(); }

    /// 是否为空
    bool empty() const { return materials_.empty(); }

    // --- GPU UBO 管理 ---

    /// 材质句柄 → UBO 偏移（字节），供 MeshDrawCommand 使用。
    /// 无效句柄回退到 index 0(DefaultPBR)。
    uint32_t materialGpuOffset(MaterialHandle handle) const;

    /// 上传所有脏材质到 GPU（每帧调用一次）。
    /// 注意：本函数不清空脏集合，以便同一帧内多个持有独立 material UBO 的
    /// pass（如实体面 GeometryPass 与边线 GeometryPass 实例）都能完整上传。
    /// 调用方应在一帧内所有 pass 都执行完毕后调用 clearDirtyMaterials()。
    /// @param materialUbo 由调用方（各 GeometryPass 实例）持有和管理的 UBO
    void uploadDirtyMaterials(Buffer* materialUbo);

    /// 清空脏材质集合（一帧内所有 pass 上传完毕后调用）。
    void clearDirtyMaterials();

    /// 材质 UBO 尺寸常量
    static constexpr uint32_t kMaxMaterials = 256;
    static constexpr uint32_t kMaterialSlotStride = 256;

private:
    void rebuildNameIndex();

    std::vector<Material>                               materials_;
    std::unordered_map<std::string, size_t>            name_to_index_;
    std::set<size_t>                                   dirty_materials_;
};

} // namespace mulan::engine
