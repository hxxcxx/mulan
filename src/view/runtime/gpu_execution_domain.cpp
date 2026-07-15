/**
 * @file gpu_execution_domain.cpp
 * @brief GpuExecutionDomain 的多 Surface 公平调度与客户端协议实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "runtime/detail/gpu_execution_domain.h"

#include "runtime/detail/render_executor.h"

#include <mulan/core/log/log.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <utility>

namespace mulan::view::detail {
namespace {

std::mutex registry_mutex;
std::vector<std::weak_ptr<GpuExecutionDomain>> registry;
std::atomic<uint64_t> next_domain_id = 1;

bool compatibleConfig(const ViewConfig& lhs, const ViewConfig& rhs) {
    if (lhs.backend != rhs.backend || lhs.enableValidation != rhs.enableValidation || lhs.msaa != rhs.msaa ||
        lhs.bufferCount != rhs.bufferCount || lhs.vsync != rhs.vsync || lhs.depthBuffer != rhs.depthBuffer ||
        lhs.stencilBuffer != rhs.stencilBuffer) {
        return false;
    }
    return std::equal(std::begin(lhs.clearColor), std::end(lhs.clearColor), std::begin(rhs.clearColor));
}

}  // namespace

struct GpuExecutionDomain::PendingFrame {
    RenderSubmission submission;
    uint64_t requiredResourceSequence = 0;
};

struct GpuExecutionDomain::ControlTask {
    std::function<ResultVoid(RenderExecutor&)> execute;
    bool fatalOnFailure = false;
    bool initializesClient = false;
    bool stopsClient = false;
    uint64_t resourceSequence = 0;
    uint64_t resourceBatchId = 0;
    std::function<void()> complete;
    std::function<void(const Error&)> fail;
};

struct GpuExecutionDomain::Client {
    enum class Lifecycle : uint8_t {
        Starting,
        Ready,
        Stopping,
        Stopped,
        Failed,
    };

    explicit Client(GpuExecutionClientId value) : id(value), executor(std::make_unique<RenderExecutor>()) {}

    GpuExecutionClientId id = 0;
    std::unique_ptr<RenderExecutor> executor;
    std::deque<ControlTask> controls;
    std::optional<PendingFrame> latestFrame;
    RenderWorkerProtocol protocol;
    RenderSurfaceState surfaceState;
    Lifecycle lifecycle = Lifecycle::Starting;
};

Error GpuExecutionDomain::domainError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

Result<std::shared_ptr<GpuExecutionDomain>> GpuExecutionDomain::acquire(const ViewConfig& config) {
    std::scoped_lock registryLock(registry_mutex);
    if (config.backend != engine::GraphicsBackend::OpenGL) {
        for (auto it = registry.begin(); it != registry.end();) {
            if (auto domain = it->lock()) {
                if (compatibleConfig(domain->config_, config)) {
                    return domain;
                }
                ++it;
            } else {
                it = registry.erase(it);
            }
        }
    }

    try {
        auto domain = std::shared_ptr<GpuExecutionDomain>(new GpuExecutionDomain(config));
        if (config.backend != engine::GraphicsBackend::OpenGL) {
            registry.emplace_back(domain);
        }
        return domain;
    } catch (const std::exception& error) {
        LOG_ERROR("[GpuExecutionDomain] Thread creation failed: {}", error.what());
    } catch (...) {
        LOG_ERROR("[GpuExecutionDomain] Thread creation failed with an unknown exception");
    }
    return std::unexpected(domainError(ErrorCode::Internal, "Failed to create GPU execution domain."));
}

GpuExecutionDomain::GpuExecutionDomain(const ViewConfig& config)
    : config_(config), domain_id_(next_domain_id.fetch_add(1)), thread_([this](std::stop_token token) { run(token); }) {
    LOG_INFO("[GpuExecutionDomain] Domain created: id={}, backend={}", domain_id_, static_cast<int>(config.backend));
}

GpuExecutionDomain::~GpuExecutionDomain() {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        for (auto& [id, client] : clients_) {
            client->lifecycle = Client::Lifecycle::Stopping;
            client->latestFrame.reset();
            while (!client->controls.empty()) {
                cancelled.push_back(std::move(client->controls.front()));
                client->controls.pop_front();
            }
        }
    }
    const Error cancellation = domainError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(cancellation);
        }
    }
    thread_.request_stop();
    wake_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
    LOG_INFO("[GpuExecutionDomain] Domain destroyed: id={}", domain_id_);
}

Result<GpuExecutionClientId> GpuExecutionDomain::attachWindow(const ViewConfig& config, int width, int height) {
    return attach(
            [config, width, height](RenderExecutor& executor) { return executor.initWindow(config, width, height); });
}

Result<GpuExecutionClientId> GpuExecutionDomain::attachOffscreen(const ViewConfig& config, int width, int height) {
    return attach([config, width, height](RenderExecutor& executor) {
        return executor.initOffscreen(config, width, height);
    });
}

Result<GpuExecutionClientId> GpuExecutionDomain::attach(Initializer initialize) {
    auto promise = std::make_shared<std::promise<ResultVoid>>();
    auto future = promise->get_future();
    GpuExecutionClientId clientId = 0;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return std::unexpected(domainError(ErrorCode::InvalidArg, "GPU execution domain is stopping."));
        }
        clientId = next_client_++;
        if (clientId == 0) {
            clientId = next_client_++;
        }
        auto client = std::make_shared<Client>(clientId);
        client->controls.push_back(ControlTask{
                .execute = std::move(initialize),
                .fatalOnFailure = true,
                .initializesClient = true,
                .complete = [promise] { promise->set_value(ResultVoid{}); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
        clients_.emplace(clientId, std::move(client));
        client_order_.push_back(clientId);
    }
    wake_.notify_one();

    try {
        ResultVoid initialized = future.get();
        if (!initialized) {
            detach(clientId);
            return std::unexpected(initialized.error());
        }
    } catch (const std::exception& error) {
        LOG_ERROR("[GpuExecutionDomain] Client initialization handshake failed: {}", error.what());
        detach(clientId);
        return std::unexpected(domainError(ErrorCode::Internal, "Render client initialization handshake failed."));
    }
    return clientId;
}

void GpuExecutionDomain::detach(GpuExecutionClientId clientId) {
    if (clientId == 0) {
        return;
    }
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        LOG_ERROR("[GpuExecutionDomain] A client cannot synchronously detach from the execution thread");
        return;
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end()) {
            return;
        }
        Client& client = *known->second;
        if (client.lifecycle == Client::Lifecycle::Stopped) {
            promise->set_value();
        } else {
            client.lifecycle = Client::Lifecycle::Stopping;
            client.latestFrame.reset();
            cancelled = std::move(client.controls);
            client.controls.push_back(ControlTask{
                    .execute = [](RenderExecutor& executor) -> ResultVoid {
                        executor.shutdown();
                        return {};
                    },
                    .stopsClient = true,
                    .complete = [promise] { promise->set_value(); },
                    .fail = [promise](const Error&) { promise->set_value(); },
            });
        }
    }
    const Error cancellation = domainError(ErrorCode::InvalidArg, "Render request was cancelled during shutdown.");
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(cancellation);
        }
    }
    wake_.notify_one();
    future.wait();

    {
        std::scoped_lock lock(mutex_);
        clients_.erase(clientId);
        std::erase(client_order_, clientId);
        cursor_.clamp(client_order_.size());
    }
}

bool GpuExecutionDomain::isReady(GpuExecutionClientId clientId) const {
    std::scoped_lock lock(mutex_);
    const auto known = clients_.find(clientId);
    return known != clients_.end() && known->second->lifecycle == Client::Lifecycle::Ready;
}

ResultVoid GpuExecutionDomain::submitFrame(GpuExecutionClientId clientId, RenderSubmission submission) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end() || known->second->lifecycle != Client::Lifecycle::Ready) {
            return std::unexpected(domainError(ErrorCode::InvalidArg, "Render client is not ready."));
        }
        Client& client = *known->second;
        auto dependency = enqueueSubmissionResourcesLocked(client, submission);
        if (!dependency) {
            return std::unexpected(dependency.error());
        }
        client.latestFrame = PendingFrame{
            .submission = std::move(submission),
            .requiredResourceSequence = *dependency,
        };
    }
    wake_.notify_one();
    return {};
}

Result<uint64_t> GpuExecutionDomain::enqueueSubmissionResourcesLocked(Client& client, RenderSubmission& submission) {
    const bool hasPrepare = !submission.prepare.empty();
    const bool hasBatch = submission.resourceBatchId != 0;
    if (hasPrepare != hasBatch) {
        return std::unexpected(domainError(
                ErrorCode::InvalidArg,
                "Render submission resource batch id and prepare payload must either both exist or both be empty."));
    }
    if (hasPrepare) {
        const ResourceRegistration registration = client.protocol.registerResourceBatch(submission.resourceBatchId);
        if (registration.newlyQueued) {
            client.latestFrame.reset();
            engine::RenderResourcePrepareList prepare = std::move(submission.prepare);
            client.controls.push_back(ControlTask{
                    .execute = [prepare = std::move(prepare)](
                                       RenderExecutor& executor) { return executor.prepareResources(prepare); },
                    .fatalOnFailure = true,
                    .resourceSequence = registration.sequence,
                    .resourceBatchId = submission.resourceBatchId,
            });
        } else {
            submission.prepare.clear();
        }
    }
    submission.prepare.clear();
    return client.protocol.currentDependency();
}

Result<engine::RenderCaptureResult> GpuExecutionDomain::capture(GpuExecutionClientId clientId,
                                                                RenderSubmission submission,
                                                                engine::RenderCaptureDesc desc) {
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(domainError(ErrorCode::InvalidArg, "Capture cannot wait on the GPU execution thread."));
    }
    using CaptureResult = Result<engine::RenderCaptureResult>;
    auto promise = std::make_shared<std::promise<CaptureResult>>();
    auto outcome = std::make_shared<std::optional<CaptureResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end() || known->second->lifecycle != Client::Lifecycle::Ready) {
            return std::unexpected(domainError(ErrorCode::InvalidArg, "Render client is not available."));
        }
        Client& client = *known->second;
        auto dependency = enqueueSubmissionResourcesLocked(client, submission);
        if (!dependency) {
            return std::unexpected(dependency.error());
        }
        client.controls.push_back(ControlTask{
                .execute = [submission = std::move(submission), desc,
                            outcome](RenderExecutor& executor) mutable -> ResultVoid {
                    *outcome = executor.capture(submission, desc);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .complete = [promise, outcome] { promise->set_value(std::move(outcome->value())); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
    }
    wake_.notify_one();
    return future.get();
}

Result<RenderSurfaceState> GpuExecutionDomain::resize(GpuExecutionClientId clientId, int width, int height) {
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        return std::unexpected(domainError(ErrorCode::InvalidArg, "Resize cannot wait on the GPU execution thread."));
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(domainError(ErrorCode::InvalidArg, "Resize dimensions must be positive."));
    }
    using ResizeResult = Result<RenderSurfaceState>;
    auto promise = std::make_shared<std::promise<ResizeResult>>();
    auto outcome = std::make_shared<std::optional<ResizeResult>>();
    auto future = promise->get_future();
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end() || known->second->lifecycle != Client::Lifecycle::Ready) {
            return std::unexpected(domainError(ErrorCode::InvalidArg, "Render client is not available."));
        }
        Client& client = *known->second;
        client.latestFrame.reset();
        client.controls.push_back(ControlTask{
                .execute = [width, height, outcome](RenderExecutor& executor) -> ResultVoid {
                    *outcome = executor.resize(width, height);
                    if (!outcome->value()) {
                        return std::unexpected(outcome->value().error());
                    }
                    return {};
                },
                .fatalOnFailure = true,
                .complete = [promise, outcome] { promise->set_value(std::move(outcome->value())); },
                .fail = [promise](const Error& error) { promise->set_value(std::unexpected(error)); },
        });
    }
    wake_.notify_one();
    return future.get();
}

void GpuExecutionDomain::enableIBL(GpuExecutionClientId clientId, std::string hdrPath) {
    if (!enqueue(clientId, ControlTask{
                                   .execute = [path = std::move(hdrPath)](RenderExecutor& executor) -> ResultVoid {
                                       executor.enableIBL(path);
                                       return {};
                                   },
                           })) {
        LOG_WARN("[GpuExecutionDomain] IBL request ignored because the client is not ready");
    }
}

ResultVoid GpuExecutionDomain::clearAssetResources(GpuExecutionClientId clientId) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end() || known->second->lifecycle != Client::Lifecycle::Ready) {
            return std::unexpected(domainError(ErrorCode::InvalidArg, "Render client is not ready."));
        }
        Client& client = *known->second;
        client.latestFrame.reset();
        const uint64_t sequence = client.protocol.registerResourceBarrier();
        client.controls.push_back(ControlTask{
                .execute = [](RenderExecutor& executor) -> ResultVoid {
                    executor.clearAssetResources();
                    return {};
                },
                .fatalOnFailure = true,
                .resourceSequence = sequence,
        });
    }
    wake_.notify_one();
    return {};
}

bool GpuExecutionDomain::enqueue(GpuExecutionClientId clientId, ControlTask task) {
    {
        std::scoped_lock lock(mutex_);
        const auto known = clients_.find(clientId);
        if (known == clients_.end() || known->second->lifecycle != Client::Lifecycle::Ready) {
            return false;
        }
        known->second->controls.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

std::vector<RenderWorkerEvent> GpuExecutionDomain::drainEvents(GpuExecutionClientId clientId) {
    std::scoped_lock lock(mutex_);
    const auto known = clients_.find(clientId);
    return known == clients_.end() ? std::vector<RenderWorkerEvent>{} : known->second->protocol.drainEvents();
}

std::optional<Error> GpuExecutionDomain::failureSnapshot(GpuExecutionClientId clientId) const {
    std::scoped_lock lock(mutex_);
    const auto known = clients_.find(clientId);
    return known == clients_.end() ? std::nullopt : known->second->protocol.failure();
}

RenderSurfaceState GpuExecutionDomain::surfaceState(GpuExecutionClientId clientId) const {
    std::scoped_lock lock(mutex_);
    const auto known = clients_.find(clientId);
    return known == clients_.end() ? RenderSurfaceState{} : known->second->surfaceState;
}

GpuExecutionDomainStats GpuExecutionDomain::stats() const {
    std::scoped_lock lock(mutex_);
    return GpuExecutionDomainStats{
        .domainId = domain_id_,
        .clientCount = clients_.size(),
        .executedControlCount = executed_control_count_,
        .executedFrameCount = executed_frame_count_,
    };
}

bool GpuExecutionDomain::clientHasWorkLocked(const Client& client) const {
    if (!client.controls.empty()) {
        return true;
    }
    return client.lifecycle == Client::Lifecycle::Ready && client.latestFrame.has_value() &&
           client.protocol.canExecuteFrame(client.latestFrame->requiredResourceSequence);
}

bool GpuExecutionDomain::hasWorkLocked() const {
    return std::any_of(clients_.begin(), clients_.end(),
                       [this](const auto& entry) { return clientHasWorkLocked(*entry.second); });
}

std::shared_ptr<GpuExecutionDomain::Client> GpuExecutionDomain::selectReadyClientLocked(
        bool& hasControl, ControlTask& control, std::optional<PendingFrame>& frame) {
    hasControl = false;
    if (client_order_.empty()) {
        return {};
    }
    const size_t count = client_order_.size();
    const size_t start = cursor_.start(count);
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = (start + offset) % count;
        const auto known = clients_.find(client_order_[index]);
        if (known == clients_.end() || !clientHasWorkLocked(*known->second)) {
            continue;
        }
        std::shared_ptr<Client> client = known->second;
        cursor_.selected(index, count);
        if (!client->controls.empty()) {
            control = std::move(client->controls.front());
            client->controls.pop_front();
            hasControl = true;
        } else {
            frame = std::move(client->latestFrame);
            client->latestFrame.reset();
        }
        return client;
    }
    return {};
}

void GpuExecutionDomain::publishSurfaceStateLocked(Client& client) {
    client.surfaceState = client.executor->surfaceState();
}

void GpuExecutionDomain::failClient(const std::shared_ptr<Client>& client, const Error& error,
                                    uint64_t resourceSequence, uint64_t resourceBatchId) {
    std::deque<ControlTask> cancelled;
    {
        std::scoped_lock lock(mutex_);
        client->latestFrame.reset();
        client->protocol.fail(error, resourceSequence, resourceBatchId);
        if (client->lifecycle == Client::Lifecycle::Stopping) {
            return;
        }
        client->lifecycle = Client::Lifecycle::Failed;
        cancelled = std::move(client->controls);
    }
    for (ControlTask& task : cancelled) {
        if (task.fail) {
            task.fail(error);
        }
    }
    wake_.notify_all();
}

void GpuExecutionDomain::run(std::stop_token stopToken) {
    {
        std::scoped_lock lock(mutex_);
        execution_thread_id_ = std::this_thread::get_id();
    }
    while (!stopToken.stop_requested()) {
        bool hasControl = false;
        ControlTask control;
        std::optional<PendingFrame> frame;
        std::shared_ptr<Client> client;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stopToken, [this] { return stopping_ || hasWorkLocked(); });
            if (stopToken.stop_requested() || stopping_) {
                break;
            }
            client = selectReadyClientLocked(hasControl, control, frame);
        }
        if (!client) {
            continue;
        }

        if (!hasControl) {
            if (!frame) {
                continue;
            }
            try {
                auto rendered = client->executor->executeFrame(frame->submission);
                if (!rendered) {
                    failClient(client, rendered.error());
                    continue;
                }
                std::scoped_lock lock(mutex_);
                ++executed_frame_count_;
                publishSurfaceStateLocked(*client);
            } catch (...) {
                failClient(client, domainError(ErrorCode::Internal, "Visual frame execution threw an exception."));
            }
            continue;
        }

        try {
            auto executed = control.execute(*client->executor);
            if (!executed) {
                if (control.fail) {
                    control.fail(executed.error());
                }
                if (control.fatalOnFailure) {
                    failClient(client, executed.error(), control.resourceSequence, control.resourceBatchId);
                }
                continue;
            }

            bool protocolCompleted = true;
            {
                std::scoped_lock lock(mutex_);
                ++executed_control_count_;
                if (control.stopsClient) {
                    client->lifecycle = Client::Lifecycle::Stopped;
                    client->surfaceState = {};
                } else {
                    publishSurfaceStateLocked(*client);
                    if (control.initializesClient) {
                        client->lifecycle = Client::Lifecycle::Ready;
                    }
                    if (control.resourceSequence != 0) {
                        protocolCompleted =
                                client->protocol.completeResource(control.resourceSequence, control.resourceBatchId);
                    }
                }
            }
            if (!protocolCompleted) {
                const Error failure = domainError(ErrorCode::Internal,
                                                  "Render resource completion violated reliable queue ordering.");
                if (control.fail) {
                    control.fail(failure);
                }
                failClient(client, failure, control.resourceSequence, control.resourceBatchId);
                continue;
            }
            if (control.complete) {
                control.complete();
            }
        } catch (const std::exception& error) {
            LOG_CRITICAL("[GpuExecutionDomain] Control task failed: {}", error.what());
            const Error failure = domainError(ErrorCode::Internal, "GPU execution control task threw an exception.");
            if (control.fail) {
                control.fail(failure);
            }
            failClient(client, failure, control.resourceSequence, control.resourceBatchId);
        } catch (...) {
            const Error failure =
                    domainError(ErrorCode::Internal, "GPU execution control task threw an unknown exception.");
            if (control.fail) {
                control.fail(failure);
            }
            failClient(client, failure, control.resourceSequence, control.resourceBatchId);
        }
    }

    std::vector<std::shared_ptr<Client>> remaining;
    {
        std::scoped_lock lock(mutex_);
        remaining.reserve(clients_.size());
        for (auto& [id, client] : clients_) {
            remaining.push_back(client);
        }
    }
    for (const auto& client : remaining) {
        client->executor->shutdown();
    }
}

}  // namespace mulan::view::detail
