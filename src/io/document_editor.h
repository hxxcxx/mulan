/**
 * @file document_editor.h
 * @brief DocumentEditor 提供面向命令的 Document 编辑操作。
 * @author hxxcxx
 * @date 2026-07-07
 *
 * Document 拥有数据容器。DocumentEditor 承担编辑意图，例如根据结构化图元
 * 创建或更新曲线资产。
 */
#pragma once

#include "io_export.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/scene/entity_id.h>

#include <string>
#include <vector>

namespace mulan::io {

class Document;

struct CurveCreateResult {
    scene::EntityId entity;
    asset::CurveElementId element;

    bool valid() const { return entity && element.valid(); }
    explicit operator bool() const { return valid(); }
};

class IO_API DocumentEditor {
public:
    explicit DocumentEditor(Document& document) : document_(document) {}

    CurveCreateResult createCurve(std::string name, asset::CurvePrimitive primitive);
    scene::EntityId createFace(std::string name, asset::FaceDefinition face);
    scene::EntityId createMesh(std::string name, std::vector<asset::MeshPrimitive> primitives);
    bool updateCurve(scene::EntityId entity, asset::CurveElementId element, asset::CurvePrimitive primitive);
    bool updateEntityTransform(scene::EntityId entity, const math::Mat4& worldTransform);
    scene::EntityId copyEntityWithTransform(scene::EntityId source, const math::Mat4& worldTransform);
    bool removeEntity(scene::EntityId entity);

private:
    asset::CurveAsset* curveAssetFor(scene::EntityId entity) const;

    Document& document_;
};

}  // namespace mulan::io
