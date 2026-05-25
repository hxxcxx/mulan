/**
 * @file InteractionHost.h
 * @brief 模态交互宿主接口
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 设计思路：
 *  - InteractionHost 是模态事件循环的抽象接口
 *  - 由 app 层实现（如 QtInteractionHost 使用 QEventLoop）
 *  - engine 层通过此接口启动模态循环，不依赖任何 UI 框架
 *  - 同一时间只允许一个模态交互运行
 */
#pragma once

namespace mulan::engine {

class Operator;
class Camera;

/// 模态交互完成状态
enum class InteractionStatus {
    Normal  = 0,   ///< 正常完成
    Cancel  = 1,   ///< 用户取消（ESC / 右键）
    Error   = 2,   ///< 系统错误
};

/// 模态交互宿主 — 负责运行 Operator 直到完成
///
/// 用法：
///   auto status = host.run(someOperator, camera);  // 阻塞
///   if (status == InteractionStatus::Normal) { ... }
class InteractionHost {
public:
    virtual ~InteractionHost() = default;

    /// 启动模态循环，阻塞直到 Operator 调用 finish() 或被取消
    /// @param op    要运行的交互操作器
    /// @param cam   相机引用（传给 Operator 事件）
    /// @return      交互完成状态
    virtual InteractionStatus run(Operator& op, Camera& cam) = 0;

    /// 中断当前正在运行的交互
    virtual void abort() = 0;

    /// 是否有交互正在运行
    virtual bool isRunning() const = 0;
};

} // namespace mulan::engine
