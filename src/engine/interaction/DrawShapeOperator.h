/**
 * @file DrawShapeOperator.h
 * @brief 拖动绘制几何体的交互算子
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 交互流程（参照老系统 DrawRectangle3D）：
 *   1. 左键点击 → 射线与工作平面求交 → 记录起点
 *   2. 拖动 → 实时计算终点 → 生成预览 Mesh → 更新 SceneNode
 *   3. 左键松开 → 创建 Entity + Geometry → 加入 Document
 *
 * 通过 setShapeType() 切换绘制类型（Box/Sphere/Cylinder...）
 */
#pragma once

#include "../../interaction/Operator.h"
#include "../../scene/camera/Camera.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../geometry/PrimitiveMesh.h"

#include "mulan/Document/Document.h"
#include "mulan/Document/Entity.h"
#include "mulan/Document/BoxGeometry.h"
#include "mulan/Document/CylinderGeometry.h"
#include "mulan/Document/SphereGeometry.h"
#include "mulan/Document/ConeGeometry.h"
#include "mulan/Document/MeshGeometry.h"

#include <memory>
#include <optional>

namespace mulan::Engine {

/// 绘制形状类型
enum class DrawShapeType {
    Box,
    Sphere,
    Cylinder,
    Cone,
};

class DrawShapeOperator : public Operator {
public:
    DrawShapeOperator(Scene& scene, document::Document& doc)
        : m_scene(scene), m_doc(doc) {}

    /// 设置绘制类型
    void setShapeType(DrawShapeType type) { m_shapeType = type; }

    /// 设置工作平面（默认 XZ 平面, Y=0）
    void setWorkPlane(const Vec3& normal, double distance) {
        m_workPlaneNormal = glm::normalize(normal);
        m_workPlaneDist   = distance;
    }

    // ==================== Operator 接口 ====================

    void onActivate(Camera& cam) override {
        (void)cam;
        m_drawing = false;
        m_previewNode = nullptr;
    }

    void onDeactivate(Camera& cam) override {
        (void)cam;
        cleanupPreview();
    }

    bool onMousePress(const InputEvent& e, Camera& cam) override {
        if (e.button != InputEvent::MouseButton::Left) return false;

        auto hit = hitWorkPlane(cam, e.x, e.y);
        if (!hit) return false;

        m_startPoint = *hit;
        m_currentPoint = *hit;
        m_drawing = true;

        // 创建预览 SceneNode
        auto mesh = buildPreviewMesh(m_startPoint, m_currentPoint);
        if (mesh) {
            m_previewNode = SceneNode::create(SceneNode::NodeType::Geometry, "preview", 0);
            m_previewNode->setCachedRenderGeometry(mesh->asRenderGeometry());
            m_previewNode->setLocalBoundingBox(mesh->bounds);
            m_scene.root()->addChild(std::unique_ptr<SceneNode>(m_previewNode));
        }
        return true;
    }

    bool onMouseMove(const InputEvent& e, Camera& cam) override {
        if (!m_drawing || !m_previewNode) return false;

        auto hit = hitWorkPlane(cam, e.x, e.y);
        if (!hit) return false;

        m_currentPoint = *hit;

        // 更新预览几何
        auto mesh = buildPreviewMesh(m_startPoint, m_currentPoint);
        if (mesh) {
            m_previewNode->updateRenderGeometry(mesh->asRenderGeometry());
            m_previewNode->setLocalBoundingBox(mesh->bounds);
        }
        return true;
    }

    bool onMouseRelease(const InputEvent& e, Camera& cam) override {
        if (!m_drawing) return false;
        (void)cam;

        m_drawing = false;

        // 检查是否有有效尺寸
        double dist = glm::length(m_currentPoint - m_startPoint);
        if (dist < 0.01) {
            cleanupPreview();
            return false;
        }

        // 创建正式 Entity 并加入 Document
        commitShape(m_startPoint, m_currentPoint);

        cleanupPreview();
        return true;
    }

    bool onKeyPress(const InputEvent& e, Camera& cam) override {
        // Escape 取消绘制
        if (e.key == InputEvent::Key::Escape && m_drawing) {
            (void)cam;
            m_drawing = false;
            cleanupPreview();
            return true;
        }
        return false;
    }

private:
    /// 射线与工作平面求交
    std::optional<Vec3> hitWorkPlane(const Camera& cam, int x, int y) const {
        Camera::Ray ray = cam.screenRay(x, y);
        return Camera::rayPlaneIntersect(ray, m_workPlaneNormal, m_workPlaneDist);
    }

    /// 根据类型和起终点生成预览 Mesh
    std::unique_ptr<Mesh> buildPreviewMesh(const Vec3& p0, const Vec3& p1) const {
        switch (m_shapeType) {
        case DrawShapeType::Box: {
            double dx = std::abs(p1.x - p0.x);
            double dz = std::abs(p1.z - p0.z);
            double dy = std::max(dx, dz) * 0.5; // 默认高度
            auto mesh = PrimitiveMesh::box(
                dx > 0.01 ? dx : 0.01,
                dy > 0.01 ? dy : 0.01,
                dz > 0.01 ? dz : 0.01);
            // 平移到正确位置
            Vec3 center((p0.x + p1.x) * 0.5, dy * 0.5, (p0.z + p1.z) * 0.5);
            applyTransform(*mesh, glm::translate(Mat4(1.0), center));
            return mesh;
        }
        case DrawShapeType::Sphere: {
            double r = glm::length(p1 - p0) * 0.5;
            auto mesh = PrimitiveMesh::sphere(r > 0.01 ? r : 0.01);
            Vec3 center = (p0 + p1) * 0.5;
            center.y = r;
            applyTransform(*mesh, glm::translate(Mat4(1.0), center));
            return mesh;
        }
        case DrawShapeType::Cylinder: {
            double r = glm::length(Vec2(p1.x - p0.x, p1.z - p0.z)) * 0.5;
            double h = r * 2.0;
            auto mesh = PrimitiveMesh::cylinder(r > 0.01 ? r : 0.01, h > 0.01 ? h : 0.01);
            Vec3 center = (p0 + p1) * 0.5;
            center.y = h * 0.5;
            applyTransform(*mesh, glm::translate(Mat4(1.0), center));
            return mesh;
        }
        case DrawShapeType::Cone: {
            double r = glm::length(Vec2(p1.x - p0.x, p1.z - p0.z)) * 0.5;
            double h = r * 2.0;
            auto mesh = PrimitiveMesh::cone(r > 0.01 ? r : 0.01, h > 0.01 ? h : 0.01);
            Vec3 center = (p0 + p1) * 0.5;
            center.y = 0;
            applyTransform(*mesh, glm::translate(Mat4(1.0), center));
            return mesh;
        }
        }
        return nullptr;
    }

    /// 将变换矩阵应用到 Mesh 顶点
    static void applyTransform(Mesh& mesh, const Mat4& transform) {
        uint32_t stride = mesh.vertexStride / sizeof(float);
        for (size_t i = 0; i < mesh.vertices.size(); i += stride) {
            Vec3 pos(mesh.vertices[i], mesh.vertices[i+1], mesh.vertices[i+2]);
            Vec4 transformed = transform * Vec4(pos, 1.0);
            mesh.vertices[i]   = static_cast<float>(transformed.x);
            mesh.vertices[i+1] = static_cast<float>(transformed.y);
            mesh.vertices[i+2] = static_cast<float>(transformed.z);
        }
        mesh.computeBounds();
    }

    /// 提交到 Document（创建参数化 Geometry + Entity）
    void commitShape(const Vec3& p0, const Vec3& p1) {
        std::unique_ptr<document::Geometry> geom;

        switch (m_shapeType) {
        case DrawShapeType::Box: {
            double dx = std::abs(p1.x - p0.x);
            double dz = std::abs(p1.z - p0.z);
            double dy = std::max(dx, dz) * 0.5;
            geom = std::make_unique<document::BoxGeometry>(
                dx > 0.01 ? dx : 0.01,
                dy > 0.01 ? dy : 0.01,
                dz > 0.01 ? dz : 0.01);
            break;
        }
        case DrawShapeType::Sphere: {
            double r = glm::length(p1 - p0) * 0.5;
            Vec3 center = (p0 + p1) * 0.5;
            center.y = r;
            geom = std::make_unique<document::SphereGeometry>(center, r);
            break;
        }
        case DrawShapeType::Cylinder: {
            double r = glm::length(Vec2(p1.x - p0.x, p1.z - p0.z)) * 0.5;
            double h = r * 2.0;
            Vec3 center = (p0 + p1) * 0.5;
            geom = std::make_unique<document::CylinderGeometry>(
                center, center + Vec3(0, h, 0), r);
            break;
        }
        case DrawShapeType::Cone: {
            double r = glm::length(Vec2(p1.x - p0.x, p1.z - p0.z)) * 0.5;
            double h = r * 2.0;
            Vec3 center = (p0 + p1) * 0.5;
            geom = std::make_unique<document::ConeGeometry>(center, r, h);
            break;
        }
        }

        if (!geom) return;

        // 创建 Entity
        auto id = document::EntityId::generate();
        std::string name = shapeTypeName();
        auto entityId = m_doc.createEntity(name, std::move(geom));

        // TODO: 通知 SceneBuilder 重建场景（添加对应 SceneNode）
        // 这里需要 UIDocument 提供 addEntityToScene() 接口
    }

    void cleanupPreview() {
        if (m_previewNode) {
            m_scene.root()->removeChild(m_previewNode);
            m_previewNode = nullptr;
        }
    }

    const char* shapeTypeName() const {
        switch (m_shapeType) {
        case DrawShapeType::Box:      return "Box";
        case DrawShapeType::Sphere:   return "Sphere";
        case DrawShapeType::Cylinder: return "Cylinder";
        case DrawShapeType::Cone:     return "Cone";
        }
        return "Shape";
    }

    // ---- 状态 ----
    Scene& m_scene;
    document::Document& m_doc;
    DrawShapeType m_shapeType = DrawShapeType::Box;

    Vec3 m_workPlaneNormal{0.0, 1.0, 0.0};
    double m_workPlaneDist = 0.0;

    Vec3 m_startPoint{0.0};
    Vec3 m_currentPoint{0.0};
    bool m_drawing = false;

    SceneNode* m_previewNode = nullptr;
};

} // namespace mulan::Engine
