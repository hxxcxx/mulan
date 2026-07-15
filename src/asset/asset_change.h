/**
 * @file asset_change.h
 * @brief 定义 AssetLibrary 内容变化 journal 的多消费者读取协议。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * AssetLibrary 只保存有界日志，每个 RenderScene/RenderWorldSync 独立持有游标。
 * 游标落后时必须全量恢复，不能把被淘汰的资产变化静默当成无变化。
 */

#pragma once

#include "asset_id.h"

#include <cstdint>
#include <vector>

namespace mulan::asset {

using AssetChangeRevision = uint64_t;
using AssetChangeDomain = uint64_t;

struct AssetChange {
    AssetChangeRevision revision = 0;
    AssetId asset;
};

struct AssetChangeCursor {
    AssetChangeDomain domain = 0;
    AssetChangeRevision revision = 0;
};

enum class AssetChangeStatus : uint8_t {
    UpToDate,
    Changes,
    FullResyncRequired,
};

struct AssetChangeSet {
    AssetChangeStatus status = AssetChangeStatus::UpToDate;
    AssetChangeDomain domain = 0;
    AssetChangeRevision toRevision = 0;
    std::vector<AssetChange> changes;

    bool requiresFullResync() const { return status == AssetChangeStatus::FullResyncRequired; }
    bool hasChanges() const { return status == AssetChangeStatus::Changes; }
    AssetChangeCursor cursorAfterApply() const { return { domain, toRevision }; }
};

}  // namespace mulan::asset
