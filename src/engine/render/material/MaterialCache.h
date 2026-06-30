/**
 * @file MaterialCache.h
 * @brief 材质资产管理器
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "Material.h"
#include "../../rhi/Device.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <set>

namespace mulan::engine {

// ============================================================
// 材质资产 — 持有材质模板（不可变）
// ============================================================

class MaterialAsset {
public:
    MaterialAsset() = default;
    explicit MaterialAsset(Material material, uint32_t id = 0);

    const Material& get() const { return m_material; }
    Material&       get()       { return m_material; }

    uint32_t           id()   const { return m_id; }
    const std::string& name() const { return m_material.name; }

    /// 转换为 GPU 可用的常量结构
    MaterialGPU toGPU() const { return MaterialGPU::fromMaterial(m_material); }

private:
    Material m_material;
    uint32_t m_id = 0;
};

// ============================================================
// 材质缓存 — 单例，管理所有材质资产
//
// 改进：
//  - 使用 unique_ptr 管理生命周期，无内存泄漏
//  - 支持按标签/类别查找
//  - 支持遍历回调
//  - 线程安全的修改标记
// ============================================================

class MaterialCache {
public:
    /// 获取全局实例
    static MaterialCache& instance();

    /// 初始化（注册默认材质）
    void init();

    // --- 材质注册 ---

    /// 注册一个新材质，返回材质 ID
    uint32_t registerMaterial(Material material);

    /// 注册材质并指定名称（可覆盖同名材质）
    uint32_t registerMaterial(const std::string& name, Material material);

    /// 获取默认 PBR 材质
    MaterialAsset* defaultPBR();

    /// 获取默认 Phong 材质
    MaterialAsset* defaultPhong();

    /// 获取线框材质
    MaterialAsset* wireframe();

    // --- 材质查找 ---

    /// 按 ID 查找
    MaterialAsset* findById(uint32_t id);

    /// 按名称查找
    MaterialAsset* findByName(const std::string& name);

    /// 获取所有材质（只读）
    const std::vector<std::unique_ptr<MaterialAsset>>& all() const { return m_materials; }

    /// 遍历所有材质
    void forEach(const std::function<void(const MaterialAsset&)>& fn) const;

    // --- 材质修改 ---

    /// 修改材质参数（按 ID），返回是否成功
    bool updateMaterial(uint32_t id, const Material& material);

    /// 修改材质参数（按名称），返回是否成功
    bool updateMaterial(const std::string& name, const Material& material);

    // --- 生命周期 ---

    /// 移除材质，返回是否成功
    bool remove(uint32_t id);

    /// 清空所有材质（保留默认材质）
    void clear();

    /// 材质数量
    size_t size() const { return m_materials.size(); }

    /// 是否为空
    bool empty() const { return m_materials.empty(); }

    // --- GPU UBO 管理 ---

    /// 设置 RHIDevice 引用（创建材质 UBO 用）
    void setDevice(RHIDevice* device);

    /// 材质 ID → UBO 偏移（字节），供 MeshDrawCommand 使用
    /// 首次查询时自动分配 slot
    uint32_t materialGpuOffset(uint32_t materialId);

    /// 获取材质 UBO buffer（供 ForwardPass / EdgePass bind）
    Buffer* materialUbo() const { return m_materialUbo.get(); }

    /// 上传所有脏材质到 GPU（每帧调用一次）
    void uploadDirtyMaterials();

private:
    MaterialCache();
    ~MaterialCache() = default;

    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;

    uint32_t allocateId();
    void     rebuildIndex();
    void     ensureUboCreated();

    std::vector<std::unique_ptr<MaterialAsset>>        m_materials;
    std::unordered_map<uint32_t, size_t>               m_idToIndex;
    std::unordered_map<std::string, size_t>            m_nameToIndex;
    uint32_t                                           m_nextId = 1;

    // GPU UBO
    RHIDevice*                                          m_device = nullptr;
    ResourcePtr<Buffer>                                  m_materialUbo;
    static constexpr uint32_t                            kMaxMaterials = 256;
    static constexpr uint32_t                            kMaterialSlotStride = 256; // 对齐到 256 bytes
    std::unordered_map<uint32_t, uint32_t>              m_materialOffsets; // id → offset
    std::set<uint32_t>                                  m_dirtyMaterials;  // 需要重新上传到 GPU 的材质 ID
};

} // namespace mulan::engine
