/**
 * @file QtInteractionHost.cpp
 * @brief Qt 模态交互宿主实现
 * @author hxxcxx
 * @date 2026-05-25
 */

#include "QtInteractionHost.h"
#include "mulan/world/Viewport.h"
#include "mulan/engine/scene/camera/Camera.h"

namespace mulan::app {

using namespace mulan::engine;

InteractionStatus QtInteractionHost::run(Operator& op, Camera& cam) {
    if (m_loop) return InteractionStatus::Error;  // 防重入

    // 取出旧 Operator（自动恢复为默认 CameraManipulator）
    std::unique_ptr<Operator> oldOp = m_view->takeOperator();

    // 设置交互操作器（不获取所有权，op 是外部引用）
    m_view->setOperatorRaw(&op);

    // 进入模态事件循环
    QEventLoop loop;
    m_loop = &loop;
    int result = loop.exec();  // 阻塞，UI 仍然响应
    m_loop = nullptr;

    // 恢复旧 Operator（先清除 raw 指针，不 delete）
    m_view->takeOperator().release();
    m_view->setOperator(std::move(oldOp));

    return static_cast<InteractionStatus>(result);
}

void QtInteractionHost::abort() {
    if (m_loop) {
        m_loop->exit(static_cast<int>(InteractionStatus::Cancel));
    }
}

bool QtInteractionHost::isRunning() const {
    return m_loop != nullptr;
}

} // namespace mulan::app
