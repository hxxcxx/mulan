/**
 * @file command.h
 * @brief 定义应用命令、命令宿主和命令执行结果。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include <mulan/core/result/error.h>

#include <string>
#include <string_view>
#include <utility>

class DocumentView;

namespace mulan::editor {

class EditorSession;

class CommandHost {
public:
    CommandHost() = default;
    CommandHost(DocumentView* documentView, EditorSession* editorSession)
        : document_view_(documentView), editor_session_(editorSession) {}

    DocumentView* documentView() const { return document_view_; }
    bool hasDocumentView() const { return document_view_ != nullptr; }

    EditorSession* editorSession() const { return editor_session_; }
    bool hasEditorSession() const { return editor_session_ != nullptr; }

private:
    DocumentView* document_view_ = nullptr;
    EditorSession* editor_session_ = nullptr;
};

using CommandOutcome = ResultVoid;

struct CommandState {
    std::string title;
    std::string shortcut;
    std::string statusText;
    bool enabled = true;
    bool visible = true;
    bool checkable = false;
    bool checked = false;
};

class Command {
public:
    virtual ~Command() = default;

    virtual std::string_view id() const = 0;
    virtual std::string_view title() const = 0;
    virtual std::string_view shortcut() const { return {}; }
    virtual std::string_view statusText() const { return {}; }

    virtual CommandState state(const CommandHost& host) const {
        (void) host;
        return CommandState{
            .title = std::string(title()),
            .shortcut = std::string(shortcut()),
            .statusText = std::string(statusText()),
            .enabled = true,
        };
    }

    CommandOutcome execute(CommandHost& host) {
        CommandState currentState = state(host);
        if (!currentState.visible) {
            std::string message = currentState.statusText.empty() ? "Command is unavailable" : currentState.statusText;
            return std::unexpected(Error::make(ErrorCode::InvalidArg, std::move(message)));
        }
        if (!currentState.enabled) {
            std::string message = currentState.statusText.empty() ? "Command is disabled" : currentState.statusText;
            return std::unexpected(Error::make(ErrorCode::InvalidArg, std::move(message)));
        }

        CommandOutcome prepared = prepare(host);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }

        CommandOutcome result = perform(host);
        cleanup(host, result);
        return result;
    }

protected:
    virtual CommandOutcome prepare(CommandHost& host) {
        (void) host;
        return {};
    }

    virtual CommandOutcome perform(CommandHost& host) = 0;

    virtual void cleanup(CommandHost& host, const CommandOutcome& result) {
        (void) host;
        (void) result;
    }
};

}  // namespace mulan::editor
