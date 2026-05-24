/**
 * @file Scene.h
 * @brief 场景管理器，管理场景节点树
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "SceneNode.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace mulan::engine {

class Scene {
public:
    Scene();

    // --- 根节点 ---

    SceneNode* root() { return m_root.get(); }
    const SceneNode* root() const { return m_root.get(); }

    // --- 添加节点 ---

    // 添加基类节点到根（装配体/分组用）
    SceneNode* addNode(std::string name, uint32_t pickId = 0);

    // 添加基类节点到指定父节点
    SceneNode* addNode(SceneNode* parent, std::string name, uint32_t pickId = 0);

    // 添加预构造的节点到根（可以是任意派生类型）
    SceneNode* addChild(std::unique_ptr<SceneNode> child);

    // 添加预构造的节点到指定父节点
    SceneNode* addChild(SceneNode* parent, std::unique_ptr<SceneNode> child);

    // --- 查找 ---

    // 按 pickId 查找节点（深度优先）
    SceneNode* findByPickId(uint32_t pickId);

    // 按名称查找（深度优先，返回第一个匹配）
    SceneNode* findByName(std::string_view name);

    // --- 变换更新 ---

    // 更新整棵树的世界变换（从根开始级联）
    void updateWorldTransforms();

    // --- 遍历（回调式）---

    using NodeCallback = std::function<void(SceneNode&)>;

    // 遍历所有节点（深度优先）
    void traverse(const NodeCallback& callback);

    // 只遍历可见节点（跳过不可见子树）
    void traverseVisible(const NodeCallback& callback);

    // --- 统计 ---

    size_t nodeCount() const;

    // --- 清空 ---

    void clear();

private:
    // 深度优先查找
    SceneNode* findByPickId(SceneNode* node, uint32_t pickId);
    SceneNode* findByName(SceneNode* node, std::string_view name);

    // 级联更新世界变换
    void updateWorldTransform(SceneNode* node, const Mat4& parentWorld);

    // 遍历所有节点
    void traverseImpl(SceneNode* node, const NodeCallback& callback);

    // 遍历可见节点
    void traverseVisibleImpl(SceneNode* node, const NodeCallback& callback);

    void countNodes(const SceneNode* node, size_t& count) const;

    std::unique_ptr<SceneNode> m_root;
};

} // namespace mulan::Engine
