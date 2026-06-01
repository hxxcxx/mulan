/**
 * @file QtInteractionHost.h
 * @brief Qt 模态交互宿主 — 使用 QEventLoop 实现阻塞
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 设计思路：
 *  - 实现 InteractionHost 接口
 *  - 使用 QEventLoop 阻塞调用线程，Qt UI 仍然响应
 *  - 持有 EngineView 指针，负责替换/恢复 Operator
 *  - 同一时间只允许一个模态交互运行（防重入）
 */
#pragma once

#include "mulan/engine/interaction/InteractionHost.h"
#include "mulan/engine/interaction/Operator.h"

#include <QObject>
#include <QEventLoop>

namespace mulan::world {
class Viewport;
}

namespace mulan::engine {
class Camera;
}

namespace mulan::app {

class QtInteractionHost : public QObject, public mulan::engine::InteractionHost {
    Q_OBJECT
public:
    explicit QtInteractionHost(mulan::world::Viewport* view, QObject* parent = nullptr)
        : QObject(parent), m_view(view) {}

    mulan::engine::InteractionStatus run(mulan::engine::Operator& op,
                                          mulan::engine::Camera& cam) override;
    void abort() override;
    bool isRunning() const override;

private:
    mulan::world::Viewport* m_view;
    QEventLoop* m_loop = nullptr;
};

} // namespace mulan::app
