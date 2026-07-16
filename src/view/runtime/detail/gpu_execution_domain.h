/**
 * @file gpu_execution_domain.h
 * @brief GpuExecutionDomain 在兼容 Device 范围内统一调度多个渲染表面
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 每个客户端保留独立的可靠控制队列、资源协议和 latest-frame 邮箱；执行域只使用
 * 一条 GPU 线程，以轮转方式公平执行各 Surface。OpenGL 上下文不共享执行域。
 */

#pragma once

#include "render_surface_state.h"
#include "render_worker_protocol.h"

#include "scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/view/core/view_config.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mulan::view::detail {

class RenderExecutor;

using GpuExecutionClientId = uint64_t;

enum class GpuExecutionDomainState : uint8_t {
    Healthy,
    Failed,
    Rebuilding,
    Stopped,
};

struct GpuExecutionDomainStats {
    uint64_t domainId = 0;
    size_t clientCount = 0;
    size_t executedControlCount = 0;
    size_t executedFrameCount = 0;
    size_t rejectedWorkCount = 0;
    size_t failureBroadcastCount = 0;
    size_t pendingControlCount = 0;
    size_t pendingFrameCount = 0;
    GpuExecutionDomainState state = GpuExecutionDomainState::Stopped;
};

/// 轮转游标只负责公平起点；调度器仍会跳过当前无任务的客户端。
class FairClientCursor {
public:
    size_t start(size_t count) const { return count == 0 ? 0 : next_ % count; }
    void selected(size_t index, size_t count) { next_ = count == 0 ? 0 : (index + 1) % count; }
    void clamp(size_t count) { next_ = count == 0 ? 0 : next_ % count; }

private:
    size_t next_ = 0;
};

class GpuExecutionDomain : public std::enable_shared_from_this<GpuExecutionDomain> {
public:
    static Result<std::shared_ptr<GpuExecutionDomain>> acquire(const ViewConfig& config);
    ~GpuExecutionDomain();

    GpuExecutionDomain(const GpuExecutionDomain&) = delete;
    GpuExecutionDomain& operator=(const GpuExecutionDomain&) = delete;

    Result<GpuExecutionClientId> attachWindow(const ViewConfig& config, int width, int height);
    Result<GpuExecutionClientId> attachOffscreen(const ViewConfig& config, int width, int height);
    void detach(GpuExecutionClientId client);

    bool isReady(GpuExecutionClientId client) const;
    ResultVoid submitFrame(GpuExecutionClientId client, RenderSubmission submission);
    Result<engine::RenderCaptureResult> capture(GpuExecutionClientId client, RenderSubmission submission,
                                                engine::RenderCaptureDesc desc);
    Result<RenderSurfaceState> resize(GpuExecutionClientId client, int width, int height);
    void enableIBL(GpuExecutionClientId client, std::string hdrPath);
    ResultVoid clearAssetResources(GpuExecutionClientId client);

    std::vector<RenderWorkerEvent> drainEvents(GpuExecutionClientId client);
    std::optional<Error> failureSnapshot(GpuExecutionClientId client) const;
    RenderSurfaceState surfaceState(GpuExecutionClientId client) const;
    GpuExecutionDomainStats stats() const;
    GpuExecutionDomainState state() const;

    /// 无真实 GPU 的状态机测试入口；仅供测试代码显式调用。
    void injectFailureForTesting(Error error);

private:
    struct Client;
    struct ControlTask;
    struct PendingFrame;
    using Initializer = std::function<ResultVoid(RenderExecutor&)>;

    explicit GpuExecutionDomain(const ViewConfig& config);

    Result<GpuExecutionClientId> attach(Initializer initialize);
    void run(std::stop_token stopToken);
    std::shared_ptr<Client> selectReadyClientLocked(bool& hasControl, ControlTask& control,
                                                    std::optional<PendingFrame>& frame);
    bool hasWorkLocked() const;
    bool clientHasWorkLocked(const Client& client) const;
    void assertInvariantsLocked() const;
    bool enqueue(GpuExecutionClientId client, ControlTask task);
    Result<uint64_t> enqueueSubmissionResourcesLocked(Client& client, RenderSubmission& submission);
    void publishSurfaceStateLocked(Client& client);
    void failClient(const std::shared_ptr<Client>& client, const Error& error, uint64_t resourceSequence = 0,
                    uint64_t resourceBatchId = 0);
    void failDomain(const Error& error);
    static bool isDeviceDomainFailure(const Error& error);
    static Error domainError(ErrorCode code, std::string_view message);

    ViewConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::unordered_map<GpuExecutionClientId, std::shared_ptr<Client>> clients_;
    std::vector<GpuExecutionClientId> client_order_;
    FairClientCursor cursor_;
    std::thread::id execution_thread_id_;
    GpuExecutionClientId next_client_ = 1;
    uint64_t domain_id_ = 0;
    size_t executed_control_count_ = 0;
    size_t executed_frame_count_ = 0;
    size_t rejected_work_count_ = 0;
    size_t failure_broadcast_count_ = 0;
    GpuExecutionDomainState state_ = GpuExecutionDomainState::Rebuilding;
    std::optional<Error> domain_failure_;
    bool stopping_ = false;
    // 必须最后声明并在构造函数体启动，禁止线程观察到尚未构造的状态成员。
    std::jthread thread_;
};

}  // namespace mulan::view::detail
