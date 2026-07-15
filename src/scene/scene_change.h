/**
 * @file scene_change.h
 * @brief SceneChange —— 场景增量变更日志及多消费者游标协议
 * @author hxxcxx
 * @date 2026-07-15
 *
 * Scene 只追加有界变更日志，不替任何消费者保存消费状态。每个消费者持有自己的
 * SceneChangeCursor，因此读取互不影响；游标落后于日志保留窗口时，Scene 会明确
 * 返回 FullResyncRequired，调用方必须全量重建，不能把缺失增量误判为“无变化”。
 * “多消费者”描述的是独立消费进度，不代表 Scene 支持并发访问；发布和读取仍须
 * 遵守 Scene 所属线程约束，或由更上层提供同步。
 */

#pragma once

#include "entity_dirty.h"
#include "entity_id.h"

#include <cstdint>
#include <vector>

namespace mulan::scene {

using SceneRevision = uint64_t;
using SceneChangeDomain = uint64_t;

/// 日志中的一次发布。entity 保留发布时的完整 generation，销毁后仍可安全识别旧实体。
struct SceneChange {
    SceneRevision revision = 0;
    EntityId entity = EntityId::invalid();
    EntityDirty dirty = EntityDirty::None;
};

/// 由消费者独占的读取位置；domain 防止跨 Scene 误用，不得在消费者之间共享。
struct SceneChangeCursor {
    SceneChangeDomain domain = 0;
    SceneRevision revision = 0;
};

enum class SceneChangeStatus : uint8_t {
    UpToDate,
    Changes,
    FullResyncRequired,
};

/// 一次非破坏读取的结果；成功应用后用 cursorAfterApply() 显式确认。
struct SceneChangeSet {
    SceneChangeStatus status = SceneChangeStatus::UpToDate;
    SceneChangeDomain domain = 0;
    SceneRevision fromRevision = 0;
    SceneRevision toRevision = 0;
    std::vector<SceneChange> changes;

    bool requiresFullResync() const { return status == SceneChangeStatus::FullResyncRequired; }
    bool hasChanges() const { return status == SceneChangeStatus::Changes; }
    SceneChangeCursor cursorAfterApply() const { return SceneChangeCursor{ domain, toRevision }; }
};

}  // namespace mulan::scene
