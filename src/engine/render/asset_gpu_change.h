/**
 * @file asset_gpu_change.h
 * @brief 定义 AssetGpuRegistry 结构化资源变更的多消费者 journal 协议。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * Registry 只保留有界日志，每个 RenderCompiler 独立持有游标。读取不会推进或
 * 删除日志；消费者只有在成功应用变更或完成全量恢复后，才提交 cursorAfterApply()。
 * 多消费者表示消费进度互相独立，不代表 Registry 支持跨线程并发读写。
 */

#pragma once

#include "asset_gpu_key.h"

#include <cstdint>
#include <vector>

namespace mulan::engine {

using AssetGpuChangeRevision = uint64_t;
using AssetGpuChangeDomain = uint64_t;

/// Legacy key 无法单独区分几何与贴图，因此事件类型必须保留资源类别。
enum class AssetGpuChangeKind : uint8_t {
    GeometryUpserted,
    GeometryRetired,
    TextureUpserted,
    TextureRetired,
    InvalidateAll,
};

struct AssetGpuChange {
    AssetGpuChangeRevision revision = 0;
    AssetGpuChangeKind kind = AssetGpuChangeKind::InvalidateAll;
    /// InvalidateAll 不对应单个资源，此时 key 为无效值。
    RenderResourceKey key;
};

/// 消费者私有读取位置；domain 防止跨 Registry 或设备重建后误用旧游标。
struct AssetGpuChangeCursor {
    AssetGpuChangeDomain domain = 0;
    AssetGpuChangeRevision revision = 0;
};

enum class AssetGpuChangeStatus : uint8_t {
    UpToDate,
    Changes,
    FullResyncRequired,
};

/// 一次非破坏读取结果。FullResyncRequired 时 changes 始终为空，禁止应用残缺增量。
struct AssetGpuChangeSet {
    AssetGpuChangeStatus status = AssetGpuChangeStatus::UpToDate;
    AssetGpuChangeDomain domain = 0;
    AssetGpuChangeRevision fromRevision = 0;
    AssetGpuChangeRevision toRevision = 0;
    std::vector<AssetGpuChange> changes;

    bool requiresFullResync() const { return status == AssetGpuChangeStatus::FullResyncRequired; }
    bool hasChanges() const { return status == AssetGpuChangeStatus::Changes; }
    AssetGpuChangeCursor cursorAfterApply() const { return { domain, toRevision }; }
};

}  // namespace mulan::engine
