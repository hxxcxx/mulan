/**
 * @file system.h
 * @brief System 基类 — World 级别的逻辑更新单元
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include <cstdint>

namespace mulan::world {

class World;

class System {
public:
    virtual ~System() = default;

    /// 每帧调用，按 priority 升序执行
    virtual void update(World& world, float dt) = 0;

    /// 执行优先级（越小越先执行）
    int priority() const { return priority_; }

protected:
    explicit System(int priority = 0) : priority_(priority) {}

private:
    int priority_ = 0;
};

} // namespace mulan::world
