#include "SceneNode.h"
#include "../RHI/Buffer.h"

namespace mulan::engine {

SceneNode* SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    child->m_parent = this;
    child->markDirty(DirtyFlag::Transform);
    m_children.push_back(std::move(child));
    return m_children.back().get();
}

std::unique_ptr<SceneNode> SceneNode::removeChild(SceneNode* child) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (it->get() == child) {
            child->m_parent = nullptr;
            auto ptr = std::move(*it);
            m_children.erase(it);
            return ptr;
        }
    }
    return nullptr;
}

bool SceneNode::isEffectivelyVisible() const {
    if (!m_visible) return false;
    return m_parent ? m_parent->isEffectivelyVisible() : true;
}

void SceneNode::setVisible(bool v) {
    if (m_visible != v) {
        m_visible = v;
        markDirty(DirtyFlag::Visibility);
        // 级联标记子节点
        for (auto& child : m_children)
            child->markDirty(DirtyFlag::Visibility);
    }
}

void SceneNode::setLocalTransform(const Mat4& t) {
    m_localTransform = t;
    markDirty(DirtyFlag::Transform);
    // 级联标记子节点需要重新计算世界变换
    for (auto& child : m_children)
        child->markDirty(DirtyFlag::Transform | DirtyFlag::Bounds);
}

// ============================================================
// GPU 缓冲区 lazy 上传
// ============================================================

namespace {
GpuGeometry uploadGeometry(RHIDevice* device, const RenderGeometry& geo) {
    GpuGeometry g;
    g.vertexStride = geo.vertexStride;
    g.vertexCount  = geo.vertexCount;
    g.indexCount   = geo.indexCount;

    if (geo.vertexCount > 0 && !geo.vertexBytes.empty()) {
        g.vertexBuffer = device->createBuffer(BufferDesc::vertex(
            static_cast<uint32_t>(geo.vertexBytes.size()),
            geo.vertexBytes.data(), "GeoVB"));
    }

    if (geo.indexCount > 0 && !geo.indexBytes.empty()) {
        g.indexBuffer = device->createBuffer(BufferDesc::index(
            static_cast<uint32_t>(geo.indexBytes.size()),
            geo.indexBytes.data(), "GeoIB"));
    }

    g.uploaded = true;
    return g;
}
} // anonymous namespace

GpuGeometry* SceneNode::ensureGpuGeometry(RHIDevice* device) {
    if (!m_gpuGeo.uploaded && hasRenderData()) {
        m_gpuGeo = uploadGeometry(device, m_cachedGeo);
    }
    return m_gpuGeo.isValid() ? &m_gpuGeo : nullptr;
}

GpuGeometry* SceneNode::ensureGpuEdgeGeometry(RHIDevice* device) {
    if (!m_gpuEdgeGeo.uploaded && hasEdgeData()) {
        m_gpuEdgeGeo = uploadGeometry(device, m_cachedEdgeGeo);
    }
    return m_gpuEdgeGeo.isValid() ? &m_gpuEdgeGeo : nullptr;
}

void SceneNode::releaseGpuResources() {
    m_gpuGeo = {};
    m_gpuEdgeGeo = {};
}

} // namespace mulan::Engine
