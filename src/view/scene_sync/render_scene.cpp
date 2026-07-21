/**
 * @file render_scene.cpp
 * @brief RenderScene 生命周期与只读查询入口。
 * @author hxxcxx
 * @date 2026-07-21
 */

#include "render_scene.h"
#include "picking/geometry_query.h"
#include "picking/primitive_pick_index.h"
#include "picking/scene_spatial_index.h"

#include <atomic>
#include <algorithm>
#include <limits>
#include <memory>

namespace mulan::view {
namespace {

uint64_t allocateRenderSceneChangeDomain() {
    static std::atomic<uint64_t> next{ 1 };
    uint64_t domain = next.fetch_add(1, std::memory_order_relaxed);
    if (domain == 0) {
        domain = next.fetch_add(1, std::memory_order_relaxed);
    }
    return domain;
}

}  // namespace

RenderScene::RenderScene()
    : spatial_index_(std::make_unique<detail::SceneSpatialIndex>()),
      primitive_pick_indices_(std::make_unique<detail::PrimitivePickIndexCache>()),
      change_domain_(allocateRenderSceneChangeDomain()) {
}

RenderScene::~RenderScene() = default;

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    scene_bounds_sphere_.reset();
    proxies_.clear();
    geometry_asset_revisions_.clear();
    geometry_asset_users_.clear();
    entity_pick_ids_.clear();
    missing_geometry_assets_.clear();
    missing_geometry_asset_users_.clear();
    lights_.clear();
    all_lights_.clear();
    spatial_index_->clear();
    primitive_pick_indices_->clear();
    scene_ = nullptr;
    assets_ = nullptr;
    scene_change_cursor_ = {};
    asset_change_cursor_ = {};
    initialized_ = false;
    ++generation_;
    resetChangeJournal();
}

void RenderScene::resetChangeJournal() {
    change_journal_.clear();
    change_domain_ = allocateRenderSceneChangeDomain();
    change_revision_ = 1;
}

void RenderScene::appendChanges(const std::unordered_map<scene::EntityId, RenderProxyDirty>& entities) {
    if (entities.empty()) {
        return;
    }
    ++change_revision_;
    if (change_revision_ == 0) {
        resetChangeJournal();
        return;
    }
    constexpr size_t MaxJournalEntries = 4096;
    std::vector<std::pair<scene::EntityId, RenderProxyDirty>> orderedChanges(entities.begin(), entities.end());
    std::ranges::sort(orderedChanges,
                      [](const auto& lhs, const auto& rhs) { return lhs.first.value < rhs.first.value; });
    for (const auto& [entity, dirty] : orderedChanges) {
        change_journal_.push_back(RenderSceneChange{ change_revision_, entity, dirty });
    }
    while (change_journal_.size() > MaxJournalEntries) {
        // revision 是一次 RenderScene 同步的原子批次；容量不足时必须整批丢弃，
        // 否则消费者可能只读到同一 revision 的后半段而无法发现漏变更。
        const uint64_t discardedRevision = change_journal_.front().revision;
        while (!change_journal_.empty() && change_journal_.front().revision == discardedRevision) {
            change_journal_.pop_front();
        }
    }
}

RenderSceneChangeSet RenderScene::readChanges(const RenderSceneChangeCursor& cursor) const {
    RenderSceneChangeSet result;
    result.domain = change_domain_;
    result.toRevision = change_revision_;
    if (cursor.domain != change_domain_ || cursor.revision > change_revision_) {
        result.status = RenderSceneChangeStatus::FullResyncRequired;
        return result;
    }
    if (cursor.revision == change_revision_) {
        return result;
    }
    if (change_journal_.empty() || cursor.revision < change_journal_.front().revision - 1) {
        result.status = RenderSceneChangeStatus::FullResyncRequired;
        return result;
    }
    result.status = RenderSceneChangeStatus::Changes;
    for (const RenderSceneChange& change : change_journal_) {
        if (change.revision > cursor.revision) {
            result.changes.push_back(change);
        }
    }
    return result;
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

void RenderScene::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld, std::vector<PickResult>& out,
                                        PickQueryStats* stats) const {
    GeometryQueryWorld(*this).collectPickCandidates(ray, lineToleranceWorld, out, stats);
}

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray, double lineToleranceWorld,
                                                         PickQueryStats* stats) const {
    return GeometryQueryWorld(*this).pick(ray, lineToleranceWorld, stats);
}

}  // namespace mulan::view
