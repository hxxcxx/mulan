/**
 * @file SceneBuilder.cpp
 * @brief SceneBuilder 实现 — 从 Document 构建 Scene
 * @author hxxcxx
 * @date 2026-04-23
 */
#include "SceneBuilder.h"

#include <MulanGeo/Document/Entity.h>
#include <MulanGeo/Document/Geometry.h>
#include <MulanGeo/Engine/Geometry/Mesh.h>

namespace Doc = MulanGeo::Document;
namespace Eng = MulanGeo::Engine;

// ============================================================
// 辅助：构建上下文
// ============================================================

struct BuildContext {
    const Doc::Document& document;
    Eng::Scene& scene;
    std::unordered_map<uint64_t, Eng::SceneNode*> entityIdToNode;
};

static void buildRecursive(BuildContext& ctx) {
    // 第一遍：为每个 Entity 创建节点（都挂在根下）
    ctx.document.forEachEntity([&](const Doc::Entity& entity) {
        auto pickId = static_cast<uint32_t>(entity.id().value);

        if (entity.hasGeometry()) {
            auto geoNode = Eng::SceneNode::create(MulanGeo::NodeType::Geometry,
                entity.name().empty() ? std::string{} : std::string(entity.name()),
                pickId);

            const Doc::Geometry* geo = entity.geometry();
            const Eng::Mesh* mesh = geo->displayMesh();

            if (mesh && !mesh->empty()) {
                geoNode->setCachedRenderGeometry(mesh->asRenderGeometry());

                if (!mesh->bounds.isEmpty()) {
                    geoNode->setLocalBoundingBox(mesh->bounds);
                }
            }

            // 构建边线渲染数据
            const Eng::Mesh* edgeMesh = geo->edgeMesh();
            if (edgeMesh && !edgeMesh->empty()) {
                geoNode->setCachedEdgeGeometry(edgeMesh->asRenderGeometry());
            }

            Eng::SceneNode* rawPtr = geoNode.get();
            ctx.scene.root()->addChild(std::move(geoNode));
            ctx.entityIdToNode[entity.id().value] = rawPtr;
        } else {
            auto node = Eng::SceneNode::create(MulanGeo::NodeType::Base,
                entity.name().empty() ? std::string{} : std::string(entity.name()),
                pickId);

            Eng::SceneNode* rawPtr = node.get();
            ctx.scene.root()->addChild(std::move(node));
            ctx.entityIdToNode[entity.id().value] = rawPtr;
        }
    });

    // 第二遍：设置局部变换
    ctx.document.forEachEntity([&](const Doc::Entity& entity) {
        auto it = ctx.entityIdToNode.find(entity.id().value);
        if (it == ctx.entityIdToNode.end()) return;

        Eng::SceneNode* node = it->second;

        if (entity.localTransform() != Eng::Mat4(1.0)) {
            node->setLocalTransform(entity.localTransform());
        }
    });

    // 计算世界变换
    ctx.scene.updateWorldTransforms();

    // 计算世界包围盒
    ctx.scene.traverse([](Eng::SceneNode& node) {
        if (node.childCount() == 0) {
            const Eng::AABB& local = node.localBoundingBox();
            if (!local.isEmpty()) {
                node.setBoundingBox(local);
            }
        }
    });
}

// ============================================================
// SceneBuilder 公开接口
// ============================================================

std::unique_ptr<Eng::Scene> SceneBuilder::build(const Doc::Document* doc) {
    auto scene = std::make_unique<Eng::Scene>();

    BuildContext ctx{*doc, *scene, {}};
    buildRecursive(ctx);

    return scene;
}

std::unordered_map<uint32_t, Doc::EntityId> SceneBuilder::buildPickIdMap(const Doc::Document* doc) {
    std::unordered_map<uint32_t, Doc::EntityId> map;
    doc->forEachEntity([&](const Doc::Entity& entity) {
        uint32_t pickId = static_cast<uint32_t>(entity.id().value);
        map[pickId] = entity.id();
    });
    return map;
}
