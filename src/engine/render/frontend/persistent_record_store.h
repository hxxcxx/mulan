/**
 * @file persistent_record_store.h
 * @brief 为 RenderWorld 快照提供固定深度、结构共享的持久化记录存储。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 该类型属于 frontend 快照的实现支撑，不作为业务层直接使用。32 位记录索引被
 * 拆成四级 8 位路径。写入只复制路径上的三个分支和一个叶页，
 * 已发布快照继续持有旧根节点，因此无需复制整份世界。每级 256 个槽位属于固定
 * 常数成本；记录本体单独共享，更新叶页不会复制同页内其他对象的动态数据。
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

namespace mulan::engine::detail {

template <typename Record>
class PersistentRecordStore {
private:
    static constexpr size_t kRadix = 256u;

    struct Leaf {
        std::array<std::shared_ptr<const Record>, kRadix> records;
    };
    struct Branch1 {
        std::array<std::shared_ptr<const Leaf>, kRadix> children;
    };
    struct Branch2 {
        std::array<std::shared_ptr<const Branch1>, kRadix> children;
    };
    struct Root {
        std::array<std::shared_ptr<const Branch2>, kRadix> children;
    };

public:
    class Range {
    public:
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using iterator_concept = std::forward_iterator_tag;
            using value_type = Record;
            using difference_type = std::ptrdiff_t;
            using reference = const Record&;
            using pointer = const Record*;

            Iterator() = default;

            reference operator*() const { return *store_.find(index_); }
            pointer operator->() const { return store_.find(index_); }

            Iterator& operator++() {
                ++index_;
                seek();
                return *this;
            }

            Iterator operator++(int) {
                Iterator previous = *this;
                ++*this;
                return previous;
            }

            friend bool operator==(const Iterator& lhs, const Iterator& rhs) {
                return lhs.index_ == rhs.index_ && lhs.store_.sameVersion(rhs.store_);
            }

        private:
            friend class Range;

            Iterator(PersistentRecordStore store, uint64_t index) : store_(std::move(store)), index_(index) { seek(); }

            void seek() { index_ = store_.nextIndex(index_); }

            PersistentRecordStore store_;
            uint64_t index_ = 0;
        };

        explicit Range(PersistentRecordStore store) : store_(std::move(store)) {}

        Iterator begin() const { return Iterator(store_, 0); }
        Iterator end() const { return Iterator(store_, store_.extent_); }
        bool empty() const { return store_.count_ == 0; }
        size_t size() const { return store_.count_; }
        const Record& front() const { return *begin(); }

    private:
        PersistentRecordStore store_;
    };

    const Record* find(uint32_t index) const {
        if (!root_ || static_cast<uint64_t>(index) >= extent_) {
            return nullptr;
        }
        const auto& branch2 = root_->children[(index >> 24u) & 0xffu];
        if (!branch2) {
            return nullptr;
        }
        const auto& branch1 = branch2->children[(index >> 16u) & 0xffu];
        if (!branch1) {
            return nullptr;
        }
        const auto& leaf = branch1->children[(index >> 8u) & 0xffu];
        if (!leaf) {
            return nullptr;
        }
        return leaf->records[index & 0xffu].get();
    }

    void set(uint32_t index, Record record) {
        const bool inserted = find(index) == nullptr;
        setRecord(index, std::make_shared<const Record>(std::move(record)));
        if (inserted) {
            ++count_;
        }
        const uint64_t requiredExtent = static_cast<uint64_t>(index) + 1u;
        if (extent_ < requiredExtent) {
            extent_ = requiredExtent;
        }
    }

    bool erase(uint32_t index) {
        if (!find(index)) {
            return false;
        }
        setRecord(index, nullptr);
        --count_;
        return true;
    }

    void clear() { *this = {}; }
    size_t size() const { return count_; }
    Range records() const { return Range(*this); }
    bool sameVersion(const PersistentRecordStore& other) const {
        return root_ == other.root_ && extent_ == other.extent_;
    }

private:
    uint64_t nextIndex(uint64_t start) const {
        if (!root_ || start >= extent_)
            return extent_;

        const size_t start3 = (start >> 24u) & 0xffu;
        const size_t start2 = (start >> 16u) & 0xffu;
        const size_t start1 = (start >> 8u) & 0xffu;
        const size_t start0 = start & 0xffu;
        for (size_t i3 = start3; i3 < kRadix; ++i3) {
            const auto& branch2 = root_->children[i3];
            if (!branch2)
                continue;
            const size_t first2 = i3 == start3 ? start2 : 0u;
            for (size_t i2 = first2; i2 < kRadix; ++i2) {
                const auto& branch1 = branch2->children[i2];
                if (!branch1)
                    continue;
                const size_t first1 = i3 == start3 && i2 == start2 ? start1 : 0u;
                for (size_t i1 = first1; i1 < kRadix; ++i1) {
                    const auto& leaf = branch1->children[i1];
                    if (!leaf)
                        continue;
                    const size_t first0 = i3 == start3 && i2 == start2 && i1 == start1 ? start0 : 0u;
                    for (size_t i0 = first0; i0 < kRadix; ++i0) {
                        if (leaf->records[i0]) {
                            const uint64_t found = (static_cast<uint64_t>(i3) << 24u) |
                                                   (static_cast<uint64_t>(i2) << 16u) |
                                                   (static_cast<uint64_t>(i1) << 8u) | i0;
                            return found < extent_ ? found : extent_;
                        }
                    }
                }
            }
        }
        return extent_;
    }

    void setRecord(uint32_t index, std::shared_ptr<const Record> record) {
        auto nextRoot = root_ ? std::make_shared<Root>(*root_) : std::make_shared<Root>();
        const size_t i3 = (index >> 24u) & 0xffu;
        const size_t i2 = (index >> 16u) & 0xffu;
        const size_t i1 = (index >> 8u) & 0xffu;
        const size_t i0 = index & 0xffu;

        auto nextBranch2 = nextRoot->children[i3] ? std::make_shared<Branch2>(*nextRoot->children[i3])
                                                  : std::make_shared<Branch2>();
        auto nextBranch1 = nextBranch2->children[i2] ? std::make_shared<Branch1>(*nextBranch2->children[i2])
                                                     : std::make_shared<Branch1>();
        auto nextLeaf = nextBranch1->children[i1] ? std::make_shared<Leaf>(*nextBranch1->children[i1])
                                                  : std::make_shared<Leaf>();
        nextLeaf->records[i0] = std::move(record);
        nextBranch1->children[i1] = std::move(nextLeaf);
        nextBranch2->children[i2] = std::move(nextBranch1);
        nextRoot->children[i3] = std::move(nextBranch2);
        root_ = std::move(nextRoot);
    }

    std::shared_ptr<const Root> root_;
    uint64_t extent_ = 0;
    size_t count_ = 0;
};

}  // namespace mulan::engine::detail
