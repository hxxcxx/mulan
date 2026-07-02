#pragma once

#include "../entity_id.h"

namespace mulan::scene {

struct HierarchyComponent {
    EntityId parent = EntityId::invalid();
};

} // namespace mulan::scene

