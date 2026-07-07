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

class DocumentView;

namespace mulan::app {

class CommandHost {
public:
    CommandHost() = default;
    explicit CommandHost(DocumentView* documentView) : document_view_(documentView) {}

    DocumentView* documentView() const { return document_view_; }
    bool hasDocumentView() const { return document_view_ != nullptr; }

private:
    DocumentView* document_view_ = nullptr;
};

using CommandOutcome = core::Result<void>;

class Command {
public:
    virtual ~Command() = default;

    virtual std::string_view id() const = 0;
    virtual std::string_view title() const = 0;

    CommandOutcome execute(CommandHost& host) {
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

}  // namespace mulan::app
