/**
 * @file hierarchy_component.h
 * @brief HierarchyComponent —— 场景实体层级中的父级关系
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "../entity_id.h"

namespace mulan::scene {

struct HierarchyComponent {
    EntityId parent = EntityId::invalid();
};

} // namespace mulan::scene
