/**
 * @file EntityId.h
 * @brief 实体唯一标识符 — 全局单调递增，线程安全
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "DocumentExport.h"

#include <cstdint>
#include <functional>

namespace mulan::document {

struct DOCUMENT_API EntityId {
    uint64_t value = 0;

    bool valid() const { return value != 0; }
    explicit operator bool() const { return valid(); }

    bool operator==(const EntityId& o) const { return value == o.value; }
    bool operator!=(const EntityId& o) const { return value != o.value; }
    bool operator<(const EntityId& o) const { return value < o.value; }

    /// 生成新的唯一 ID（全局原子递增）
    static EntityId generate();

    /// 从已有值构造（用于反序列化恢复）
    static EntityId fromValue(uint64_t v) { return EntityId{v}; }
};

} // namespace mulan::document

template<>
struct std::hash<mulan::document::EntityId> {
    size_t operator()(const mulan::document::EntityId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};
