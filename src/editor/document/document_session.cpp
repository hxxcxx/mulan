#include "document_session.h"

#include "core/operation/command_history.h"

#include <mulan/core/log/log.h>

#include <utility>
#include <vector>

namespace mulan::editor {

DocumentSession::DocumentSession(std::unique_ptr<mulan::Document> doc, DocumentSessionOptions options)
    : document_(std::move(doc)),
      command_history_(std::make_unique<CommandHistory>()),
      preferences_(options.renderPreferences),
      kind_(options.kind) {
    LOG_INFO("[Editor] Document session created: name={}, kind={}", displayName(),
             kind_ == DocumentSessionKind::Draft ? "draft" : "imported");
}

DocumentSession::~DocumentSession() {
    LOG_INFO("[Editor] Document session closed: name={}, dirty={}", displayName(),
             document_ ? document_->isDirty() : false);
}

CommandHistory& DocumentSession::commandHistory() noexcept {
    return *command_history_;
}

DocumentSession::ChangeSubscriptionId DocumentSession::subscribeChanges(ChangeCallback callback) {
    if (!callback) {
        return 0;
    }
    ChangeSubscriptionId id = next_change_subscription_++;
    if (id == 0) {
        id = next_change_subscription_++;
    }
    change_callbacks_.emplace(id, std::move(callback));
    return id;
}

void DocumentSession::unsubscribeChanges(ChangeSubscriptionId subscription) {
    if (subscription != 0) {
        change_callbacks_.erase(subscription);
    }
}

DocumentChangeStamp DocumentSession::publishChange(DocumentChangeKind kinds) {
    if (kinds == DocumentChangeKind::None) {
        return {};
    }
    ++change_revision_;
    if (change_revision_ == 0) {
        change_revision_ = 1;
    }
    const DocumentChangeStamp stamp{ .revision = change_revision_, .kinds = kinds };

    // 回调允许在通知过程中解除自身订阅，因此先复制当前回调集合。
    std::vector<ChangeCallback> callbacks;
    callbacks.reserve(change_callbacks_.size());
    for (const auto& [id, callback] : change_callbacks_) {
        (void) id;
        callbacks.push_back(callback);
    }
    for (const auto& callback : callbacks) {
        callback(stamp);
    }
    return stamp;
}

const std::string& DocumentSession::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}

}  // namespace mulan::editor
