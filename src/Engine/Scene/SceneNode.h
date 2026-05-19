/**
 * @file SceneNode.h
 * @brief 场景节点基类，层级、变换与可见性管理
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../Math/Math.h"
#include "../Math/AABB.h"
#include "../Render/RenderGeometry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace MulanGeo {

// 节点类型枚举
enum class NodeType : uint8_t {
    Base      = 0,
    Geometry  = 1,
    Group     = 2,
};

} // namespace MulanGeo

namespace MulanGeo::Engine {

class RHIDevice;  // 前向声明

// ============================================================
// 场景节点 — 层级、变换、可见性、几何数据
// ============================================================

class Scene;  // 友元前向声明

class SceneNode {
    friend class Scene;
public:
    ~SceneNode() = default;

    // --- 脏标记 ---

    enum class DirtyFlag : uint8_t {
        None       = 0,
        Transform  = 1 << 0,
        Visibility = 1 << 1,
        Bounds     = 1 << 2,
        All        = Transform | Visibility | Bounds,
    };

    // --- 类型查询 ---

    MulanGeo::NodeType type() const { return m_type; }

    bool isType(MulanGeo::NodeType t) const { return m_type == t; }

    // --- 构造 ---

    static std::unique_ptr<SceneNode> create(MulanGeo::NodeType type, std::string name = {}, uint32_t pickId = 0) {
        return std::unique_ptr<SceneNode>(new SceneNode(type, std::move(name), pickId));
    }

private:
    explicit SceneNode(MulanGeo::NodeType type, std::string name = {}, uint32_t pickId = 0)
        : m_type(type), m_name(std::move(name)), m_pickId(pickId) {}

public:
    // 禁止拷贝
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;

    // --- 层级 ---

    SceneNode* parent() const { return m_parent; }

    const std::vector<std::unique_ptr<SceneNode>>& children() const {
        return m_children;
    }

    // 添加子节点，返回原始指针
    SceneNode* addChild(std::unique_ptr<SceneNode> child);

    // 移除并返回子节点
    std::unique_ptr<SceneNode> removeChild(SceneNode* child);

    size_t childCount() const { return m_children.size(); }

    // --- 名称 ---

    std::string_view name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // --- 可见性 ---

    bool visible() const { return m_visible; }
    void setVisible(bool v);

    // 是否实际可见（自身 + 所有祖先都可见）
    bool isEffectivelyVisible() const;

    // --- 拾取 ---

    uint32_t pickId() const { return m_pickId; }
    void setPickId(uint32_t id) { m_pickId = id; }

    // --- 变换 ---

    const Mat4& localTransform() const { return m_localTransform; }
    void setLocalTransform(const Mat4& t);

    const Mat4& worldTransform() const { return m_worldTransform; }

    // 标记世界变换需要重新计算
    void markDirty(DirtyFlag flag) { m_dirtyFlags = static_cast<DirtyFlag>(static_cast<uint8_t>(m_dirtyFlags) | static_cast<uint8_t>(flag)); }
    bool isDirty(DirtyFlag flag) const { return (static_cast<uint8_t>(m_dirtyFlags) & static_cast<uint8_t>(flag)) != 0; }
    bool hasAnyDirty() const { return m_dirtyFlags != DirtyFlag::None; }
    void clearDirty() { m_dirtyFlags = DirtyFlag::None; }

    // 兼容旧接口
    void markWorldDirty() { markDirty(DirtyFlag::Transform); }
    bool isWorldDirty() const { return isDirty(DirtyFlag::Transform); }

    // --- 包围盒 ---

    const AABB& localBoundingBox() const { return m_localBounds; }
    void setLocalBoundingBox(const AABB& bounds) { m_localBounds = bounds; markDirty(DirtyFlag::Bounds); }

    const AABB& worldBoundingBox() const { return m_worldBounds; }

    // 兼容旧接口
    const AABB& boundingBox() const { return m_worldBounds; }
    void setBoundingBox(const AABB& bounds) { m_localBounds = bounds; m_worldBounds = bounds; }

    // --- 选择状态 ---

    bool selected() const { return m_selected; }
    void setSelected(bool s) { m_selected = s; }

    // --- 几何数据（由 SceneBuilder 一次性设置）---

    void setCachedRenderGeometry(const RenderGeometry& geo) { m_cachedGeo = geo; }
    const RenderGeometry& cachedRenderGeometry() const { return m_cachedGeo; }

    /// 更新渲染几何数据并标记 GPU 需要重新上传
    void updateRenderGeometry(const RenderGeometry& geo) {
        m_cachedGeo = geo;
        releaseGpuResources();
    }

    void setCachedEdgeGeometry(const RenderGeometry& geo) { m_cachedEdgeGeo = geo; }
    const RenderGeometry& cachedEdgeGeometry() const { return m_cachedEdgeGeo; }

    GpuGeometry* ensureGpuGeometry(RHIDevice* device);
    GpuGeometry* ensureGpuEdgeGeometry(RHIDevice* device);
    void releaseGpuResources();

    bool hasRenderData() const { return m_cachedGeo.vertexCount > 0; }
    bool hasEdgeData() const { return m_cachedEdgeGeo.vertexCount > 0; }

    uint16_t materialIndex() const { return m_materialIndex; }
    void setMaterialIndex(uint16_t idx) { m_materialIndex = idx; }

private:
    MulanGeo::NodeType m_type;
    std::string m_name;
    uint32_t    m_pickId   = 0;
    bool        m_visible  = true;
    bool        m_selected = false;

    Mat4     m_localTransform = Mat4(1.0);
    Mat4     m_worldTransform = Mat4(1.0);
    DirtyFlag m_dirtyFlags    = DirtyFlag::All;

    AABB     m_localBounds;
    AABB     m_worldBounds;

    SceneNode*  m_parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> m_children;

    // 几何数据
    RenderGeometry   m_cachedGeo;
    RenderGeometry   m_cachedEdgeGeo;
    GpuGeometry      m_gpuGeo;
    GpuGeometry      m_gpuEdgeGeo;
    uint16_t         m_materialIndex = 0xFFFF;
};

// DirtyFlag 位运算
inline SceneNode::DirtyFlag operator|(SceneNode::DirtyFlag a, SceneNode::DirtyFlag b) {
    return static_cast<SceneNode::DirtyFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline SceneNode::DirtyFlag operator&(SceneNode::DirtyFlag a, SceneNode::DirtyFlag b) {
    return static_cast<SceneNode::DirtyFlag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

} // namespace MulanGeo::Engine
