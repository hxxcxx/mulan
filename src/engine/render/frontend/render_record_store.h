/**
 * @file render_record_store.h
 * @brief RenderRecordStore 为 RenderWorld 提供按稳定索引访问的稀疏记录存储。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 记录由 RenderWorldStorage 整体做写时复制。本类型只负责稳定索引、删除空洞和
 * 顺序遍历，不承担快照版本管理，也不在每次写入时构造持久化树。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace mulan::engine::detail {

template <typename Record>
class RenderRecordStore {
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

            reference operator*() const { return *(*records_)[index_]; }
            pointer operator->() const { return &*(*records_)[index_]; }

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

            friend bool operator==(const Iterator&, const Iterator&) = default;

        private:
            friend class Range;

            Iterator(const std::vector<std::optional<Record>>* records, size_t index)
                : records_(records), index_(index) {
                seek();
            }

            void seek() {
                while (records_ && index_ < records_->size() && !(*records_)[index_])
                    ++index_;
            }

            const std::vector<std::optional<Record>>* records_ = nullptr;
            size_t index_ = 0;
        };

        Range(const std::vector<std::optional<Record>>* records, size_t count) : records_(records), count_(count) {}

        Iterator begin() const { return Iterator(records_, 0); }
        Iterator end() const { return Iterator(records_, records_ ? records_->size() : 0); }
        bool empty() const { return count_ == 0; }
        size_t size() const { return count_; }
        const Record& front() const { return *begin(); }

    private:
        const std::vector<std::optional<Record>>* records_ = nullptr;
        size_t count_ = 0;
    };

    const Record* find(uint32_t index) const {
        if (index >= records_.size() || !records_[index])
            return nullptr;
        return &*records_[index];
    }

    void set(uint32_t index, Record record) {
        if (index >= records_.size())
            records_.resize(static_cast<size_t>(index) + 1u);
        if (!records_[index])
            ++count_;
        records_[index] = std::move(record);
    }

    bool erase(uint32_t index) {
        if (index >= records_.size() || !records_[index])
            return false;
        records_[index].reset();
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    Range records() const { return Range(&records_, count_); }

private:
    std::vector<std::optional<Record>> records_;
    size_t count_ = 0;
};

}  // namespace mulan::engine::detail
