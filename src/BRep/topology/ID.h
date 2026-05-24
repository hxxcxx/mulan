/**
 * @file ID.h
 * @brief 基于指针地址的拓扑元素 ID 系统
 *
 * 关键：ID 基于 shared_ptr::get() 指针地址，不是计数器。
 * 共享同一份数据的引用具有相同 ID。
 *
 * 基于 truck-topology::id::ID。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include <memory>
#include <functional>
#include <cstddef>

namespace MulanGeo::brep {

/// 基于 shared_ptr 指针地址的 ID
/// @tparam T shared_ptr 的模板参数类型
template<typename T>
class ID {
public:
    ID() : ptr_(nullptr) {}
    explicit ID(const std::shared_ptr<T>& sp) : ptr_(sp.get()) {}

    bool operator==(const ID& o) const { return ptr_ == o.ptr_; }
    bool operator!=(const ID& o) const { return ptr_ != o.ptr_; }
    bool operator<(const ID& o) const { return ptr_ < o.ptr_; }

    explicit operator bool() const { return ptr_ != nullptr; }

    struct Hash {
        size_t operator()(const ID& id) const {
            return std::hash<const void*>{}(id.ptr_);
        }
    };

private:
    const void* ptr_;
};

// 类型别名
template<typename P> using VertexID = ID<P>;
template<typename C> using EdgeID   = ID<C>;
template<typename S> using FaceID   = ID<S>;

} // namespace MulanGeo::BRep
