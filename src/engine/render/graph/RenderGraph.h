/**
 * @file RenderGraph.h
 * @brief RenderGraph 容器 — Pass 注册 + 顺序执行
 *
 * 第一期不含资源屏障和自动资源管理，只做顺序执行。
 * 后期补：Pass 声明 input/output resource → 自动插入 barrier。
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "RenderPass.h"

#include <memory>
#include <vector>

namespace mulan::engine {

class RenderGraph {
public:
    void addPass(std::unique_ptr<RenderPass> pass) {
        m_passes.push_back(std::move(pass));
    }

    void execute(const PassContext& ctx) {
        for (auto& pass : m_passes)
            pass->execute(ctx);
    }

    template<typename T>
    T* pass(int index) {
        if (index >= 0 && index < static_cast<int>(m_passes.size()))
            return dynamic_cast<T*>(m_passes[index].get());
        return nullptr;
    }

    size_t passCount() const { return m_passes.size(); }

private:
    std::vector<std::unique_ptr<RenderPass>> m_passes;
};

} // namespace mulan::engine
