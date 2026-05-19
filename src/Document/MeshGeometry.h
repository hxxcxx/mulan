/**
 * @file MeshGeometry.h
 * @brief 网格几何体 — 直接存储顶点/索引数据（用于导入的 STL/OBJ 等）
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 与参数化几何体不同，MeshGeometry 直接持有三角网格数据。
 * 适用于：外部导入的网格、扫描点云三角化等不可重建的几何。
 * 序列化时需要写完整顶点/索引数组（文件较大）。
 */
#pragma once

#include "DocumentExport.h"
#include "Geometry.h"

#include "MulanGeo/Engine/Geometry/Mesh.h"
#include "MulanGeo/Core/Serialization/Archive.h"

#include <memory>

namespace MulanGeo::Document {

class DOCUMENT_API MeshGeometry : public Geometry {
    MULANGEO_OBJECT(MeshGeometry, Geometry)

public:
    MeshGeometry() = default;

    /// 从已有 Mesh 构造（移动语义，零拷贝）
    explicit MeshGeometry(std::unique_ptr<Engine::Mesh> mesh)
        : m_mesh(std::move(mesh)) {}

    /// 从外部数据构造（导入器等使用）
    MeshGeometry(std::vector<float> vertices,
                 std::vector<uint32_t> indices,
                 const std::string& name = {})
    {
        m_mesh = std::make_unique<Engine::Mesh>();
        m_mesh->vertices = std::move(vertices);
        m_mesh->indices  = std::move(indices);
        m_mesh->name     = name;
        m_mesh->computeBounds();
    }

    // --- Geometry 接口 ---

    GeometryType geometryType() const override { return GeometryType::Mesh; }

    const Engine::Mesh* displayMesh() const override {
        return m_mesh ? m_mesh.get() : nullptr;
    }

    Engine::AABB boundingBox() const override {
        return m_mesh ? m_mesh->bounds : Engine::AABB::empty();
    }

    // --- 访问原始数据（用于序列化）---

    const std::vector<float>& vertices() const { return m_mesh->vertices; }
    const std::vector<uint32_t>& indices() const { return m_mesh->indices; }
    const std::string& meshName() const { return m_mesh->name; }

    // --- Core::Object 序列化 ---

    void serialize(Core::OutputArchive& ar) const override {
        if (!m_mesh) return;
        // 写入网格名称
        ar << m_mesh->name;
        // 写入顶点数据（使用 bulk 写入优化）
        uint32_t vCount = static_cast<uint32_t>(m_mesh->vertices.size());
        ar << vCount;
        ar.writeBytes(std::as_bytes(std::span{m_mesh->vertices}));
        // 写入索引数据
        uint32_t iCount = static_cast<uint32_t>(m_mesh->indices.size());
        ar << iCount;
        ar.writeBytes(std::as_bytes(std::span{m_mesh->indices}));
    }

    void serialize(Core::InputArchive& ar) override {
        m_mesh = std::make_unique<Engine::Mesh>();
        // 读名称
        ar >> m_mesh->name;
        // 读顶点
        uint32_t vCount = 0;
        ar >> vCount;
        m_mesh->vertices.resize(vCount);
        auto vResult = ar.readBytes(std::as_writable_bytes(std::span{m_mesh->vertices}));
        // 读索引
        uint32_t iCount = 0;
        ar >> iCount;
        m_mesh->indices.resize(iCount);
        auto iResult = ar.readBytes(std::as_writable_bytes(std::span{m_mesh->indices}));
        m_mesh->computeBounds();
    }

private:
    std::unique_ptr<Engine::Mesh> m_mesh;
};

} // namespace MulanGeo::Document
