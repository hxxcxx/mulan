/**
 * @file render_graph.h
 * @brief RenderGraph 容器 — Pass 注册 + 顺序执行
 *
 * 第一期不含资源屏障和自动资源管理，只做顺序执行。
 * 后期补：Pass 声明 input/output resource → 自动插入 barrier。
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "render_pass.h"

#include <memory>
#include <vector>

namespace mulan::engine {

class RenderGraph {
public:
    void addPass(std::unique_ptr<RenderPass> pass) {
        passes_.push_back(std::move(pass));
    }

    void execute(const PassContext& ctx) {
        for (auto& pass : passes_)
            pass->execute(ctx);
    }

    template<typename T>
    T* pass(int index) {
        if (index >= 0 && index < static_cast<int>(passes_.size()))
            return dynamic_cast<T*>(passes_[index].get());
        return nullptr;
    }

    size_t passCount() const { return passes_.size(); }

    /// 清空所有 Pass（释放其持有的 GPU 资源，需在 device 析构前调用）
    void clear() { passes_.clear(); }

private:
    std::vector<std::unique_ptr<RenderPass>> passes_;
};

} // namespace mulan::engine
