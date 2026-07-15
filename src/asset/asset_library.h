/**
 * @file asset_library.h
 * @brief AssetLibrary —— 文档层资产容器，管理可复用几何、材质与纹理数据
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "asset.h"
#include "asset_change.h"
#include "curve_asset.h"
#include "face_asset.h"
#include "tessellated_asset.h"
#include "brep_asset.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "texture_asset.h"

#include <concepts>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace mulan::asset {

using AssetLibraryRevision = uint64_t;

class AssetLibrary {
public:
    static constexpr size_t DefaultChangeJournalCapacity = 4096;

    explicit AssetLibrary(size_t changeJournalCapacity = DefaultChangeJournalCapacity);

    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    template <typename T, typename... Args>
    T* create(std::string name, Args&&... args) {
        static_assert(std::is_base_of_v<Asset, T>);
        AssetId id = allocateId();
        auto asset = std::make_unique<T>(id, std::move(name), std::forward<Args>(args)...);
        T* ptr = asset.get();
        asset->bindChangeCallback([this](AssetId changed) { appendChange(changed); });
        assets_[id] = std::move(asset);
        touch();
        appendChange(id);
        return ptr;
    }

    Asset* asset(AssetId id);
    const Asset* asset(AssetId id) const;

    bool contains(AssetId id) const;
    bool remove(AssetId id);
    void clear();

    /// 资产集合版本：仅 create、成功 remove、非空 clear 时递增。
    AssetLibraryRevision membershipRevision() const noexcept { return membership_revision_; }
    /// 进程内唯一的资产域身份；用于隔离不同文档中数值相同的 AssetId。
    uint64_t domainId() const noexcept { return domain_id_; }
    std::optional<AssetRevision> contentRevision(AssetId id) const;
    AssetChangeCursor currentChangeCursor() const { return { change_domain_, change_revision_ }; }
    AssetChangeSet readChanges(const AssetChangeCursor& cursor) const;

    /// 只读遍历不暴露容器或 unique_ptr，回调不能绕过资产 mutator 修改内容。
    template <typename Func>
        requires std::invocable<Func&, const Asset&>
    void forEachAsset(Func&& fn) const {
        for (const auto& [id, value] : assets_) {
            if (value)
                fn(static_cast<const Asset&>(*value));
        }
    }

    size_t count() const { return assets_.size(); }
    size_t count(AssetKind kind) const;

private:
    AssetId allocateId();
    void touch();
    void appendChange(AssetId id);

    AssetIdValue next_id_ = 1;
    uint64_t domain_id_ = 0;
    AssetLibraryRevision membership_revision_ = 0;
    size_t change_journal_capacity_ = DefaultChangeJournalCapacity;
    AssetChangeDomain change_domain_ = 0;
    AssetChangeRevision change_revision_ = 0;
    std::deque<AssetChange> change_journal_;
    std::unordered_map<AssetId, std::unique_ptr<Asset>> assets_;
};

}  // namespace mulan::asset
