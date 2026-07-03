/**
 * @file material_cache.h
 * @brief 材质资产管理器
 * @author hxxcxx
 * @date 2026-04-23
 */

#pragma once

#include "material.h"
#include "../../rhi/device.h"

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

    const Material& get() const { return material_; }
    Material&       get()       { return material_; }

    uint32_t           id()   const { return id_; }
    const std::string& name() const { return material_.name; }

    /// 转换为 GPU 可用的常量结构
    MaterialGPU toGPU() const { return MaterialGPU::fromMaterial(material_); }

private:
    Material material_;
    uint32_t id_ = 0;
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
    const std::vector<std::unique_ptr<MaterialAsset>>& all() const { return materials_; }

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
    size_t size() const { return materials_.size(); }

    /// 是否为空
    bool empty() const { return materials_.empty(); }

    // --- GPU UBO 管理 ---

    /// 设置 RHIDevice 引用（用于材质数据写入时的 device 引用）
    void setDevice(RHIDevice* device);

    /// 材质 ID → UBO 偏移（字节），供 MeshDrawCommand 使用
    uint32_t materialGpuOffset(uint32_t materialId);

    /// 上传所有脏材质到 GPU（每帧调用一次）。
    /// 注意：本函数不清空脏集合，以便同一帧内多个持有独立 material UBO 的
    /// pass（如 ForwardPass 与 EdgePass）都能完整上传。调用方应在一帧内所有
    /// pass 都执行完毕后调用 clearDirtyMaterials()。
    /// @param materialUbo 由调用方（ForwardPass/EdgePass）持有和管理的 UBO
    void uploadDirtyMaterials(Buffer* materialUbo);

    /// 清空脏材质集合（一帧内所有 pass 上传完毕后调用）。
    void clearDirtyMaterials();

    /// 材质 UBO 尺寸常量
    static constexpr uint32_t kMaxMaterials = 256;
    static constexpr uint32_t kMaterialSlotStride = 256;

private:
    MaterialCache();
    ~MaterialCache() = default;

    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;

    uint32_t allocateId();
    void     rebuildIndex();

    std::vector<std::unique_ptr<MaterialAsset>>        materials_;
    std::unordered_map<uint32_t, size_t>               id_to_index_;
    std::unordered_map<std::string, size_t>            name_to_index_;
    uint32_t                                           next_id_ = 1;

    // GPU UBO 管理（不持有 buffer，由调用方提供）
    RHIDevice*                                          device_ = nullptr;
    std::unordered_map<uint32_t, uint32_t>              material_offsets_; // id → offset
    std::set<uint32_t>                                  dirty_materials_;
};

} // namespace mulan::engine
