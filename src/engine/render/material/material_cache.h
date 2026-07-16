/**
 * @file material_cache.h
 * @brief 管理 engine::Material 实例与材质句柄
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 设计：
 *  - 直接存储 vector<Material>，无额外包装类型
 *  - 使用 vector 索引作为材质句柄
 *  - 由 Renderer 持有并通过引用注入
 */

#pragma once

#include "material.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

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

    /// 注册一个新材质，返回句柄（vector index）；容量耗尽时返回无效句柄。
    MaterialHandle registerMaterial(Material material);

    /// 注册材质并指定名称（同名覆盖，句柄不变）；容量耗尽时返回无效句柄。
    MaterialHandle registerMaterial(const std::string& name, Material material);

    // --- 材质查找 ---

    /// 按句柄查找
    const Material* find(MaterialHandle handle) const;
    Material* find(MaterialHandle handle);

    /// 按名称查找
    const Material* findByName(const std::string& name) const;
    Material* findByName(const std::string& name);

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

    /// 返回可观察材质集的当前版本；真实新增、替换、更新、移除或清空时递增。
    uint64_t revision() const noexcept { return revision_; }

    /// 仅在已有 MaterialHandle 的数值映射可能改变时递增，供 DrawCommand 缓存失效。
    uint64_t layoutRevision() const noexcept { return layout_revision_; }

    /// 材质数量
    size_t size() const { return materials_.size(); }

    /// 是否为空
    bool empty() const { return materials_.empty(); }

    /// MeshDrawCommand 使用 uint32_t 材质索引，因此只保留真实的可表示边界。
    static constexpr size_t kMaxMaterials = static_cast<size_t>(std::numeric_limits<uint32_t>::max());

private:
    void advanceRevision() noexcept;
    void advanceLayoutRevision() noexcept;
    void rebuildNameIndex();

    uint64_t revision_ = 1;
    uint64_t layout_revision_ = 1;
    std::vector<Material> materials_;
    std::unordered_map<std::string, size_t> name_to_index_;
};

}  // namespace mulan::engine
