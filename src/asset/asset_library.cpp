#include "asset_library.h"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace mulan::asset {

AssetLibrary::AssetLibrary(size_t changeJournalCapacity) : change_journal_capacity_(changeJournalCapacity) {
    static std::atomic<uint64_t> nextDomain{ 1 };
    domain_id_ = nextDomain.fetch_add(1, std::memory_order_relaxed);
    if (domain_id_ == 0) {
        domain_id_ = nextDomain.fetch_add(1, std::memory_order_relaxed);
    }
    change_domain_ = domain_id_;
}

AssetId AssetLibrary::allocateId() {
    return AssetId{ next_id_++ };
}

Asset* AssetLibrary::asset(AssetId id) {
    auto it = assets_.find(id);
    return it != assets_.end() ? it->second.get() : nullptr;
}

const Asset* AssetLibrary::asset(AssetId id) const {
    auto it = assets_.find(id);
    return it != assets_.end() ? it->second.get() : nullptr;
}

bool AssetLibrary::contains(AssetId id) const {
    return assets_.find(id) != assets_.end();
}

bool AssetLibrary::remove(AssetId id) {
    if (assets_.erase(id) == 0)
        return false;
    touch();
    appendChange(id);
    return true;
}

void AssetLibrary::clear() {
    if (assets_.empty())
        return;
    assets_.clear();
    touch();
    // 无法用单个 AssetId 表达 clear；空 id 明确要求消费者全量恢复。
    appendChange({});
}

std::optional<AssetRevision> AssetLibrary::contentRevision(AssetId id) const {
    const Asset* value = asset(id);
    if (!value)
        return std::nullopt;
    return value->revision();
}

void AssetLibrary::touch() {
    if (membership_revision_ == std::numeric_limits<AssetLibraryRevision>::max())
        throw std::overflow_error("AssetLibrary revision exhausted");
    ++membership_revision_;
}

void AssetLibrary::appendChange(AssetId id) {
    if (change_revision_ == std::numeric_limits<AssetChangeRevision>::max())
        throw std::overflow_error("AssetLibrary change revision exhausted");
    ++change_revision_;
    if (change_journal_capacity_ == 0)
        return;
    change_journal_.push_back({ change_revision_, id });
    while (change_journal_.size() > change_journal_capacity_)
        change_journal_.pop_front();
}

AssetChangeSet AssetLibrary::readChanges(const AssetChangeCursor& cursor) const {
    AssetChangeSet result;
    result.domain = change_domain_;
    result.toRevision = change_revision_;
    const bool foreignDomain = cursor.domain != 0 && cursor.domain != change_domain_;
    const bool unboundRevision = cursor.domain == 0 && cursor.revision != 0;
    if (foreignDomain || unboundRevision) {
        result.status = AssetChangeStatus::FullResyncRequired;
        return result;
    }
    if (cursor.revision == change_revision_) {
        return result;
    }
    const bool cursorAhead = cursor.revision > change_revision_;
    const bool journalMissing = change_journal_.empty();
    const bool cursorTooOld = !journalMissing && cursor.revision < change_journal_.front().revision - 1;
    if (cursorAhead || journalMissing || cursorTooOld) {
        result.status = AssetChangeStatus::FullResyncRequired;
        return result;
    }
    result.status = AssetChangeStatus::Changes;
    for (const AssetChange& change : change_journal_) {
        if (change.revision > cursor.revision)
            result.changes.push_back(change);
    }
    return result;
}

size_t AssetLibrary::count(AssetKind kind) const {
    size_t result = 0;
    for (const auto& [id, asset] : assets_) {
        if (asset && asset->kind() == kind)
            ++result;
    }
    return result;
}

}  // namespace mulan::asset
