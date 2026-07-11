/**
 * @file document_session.h
 * @brief 表示应用中一个已打开文档的会话状态。
 *
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/io/document.h>
#include <mulan/io/import_result.h>

#include <cstdint>
#include <memory>
#include <string>

enum class DocumentSessionKind : uint8_t {
    Draft,
    Imported,
};

struct DocumentRenderPreferences {
    bool preferOrthographic = true;
    bool preferIBL = true;
    bool preferPBRSurface = false;
};

class DocumentSession {
public:
    explicit DocumentSession(std::unique_ptr<mulan::io::Document> doc);
    DocumentSession(std::unique_ptr<mulan::io::Document> doc, mulan::io::ImportReport report);
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    mulan::io::Document* document() { return document_.get(); }
    const mulan::io::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    const DocumentRenderPreferences& renderPreferences() const { return preferences_; }
    bool preferOrthographic() const { return preferences_.preferOrthographic; }
    bool preferIBL() const { return preferences_.preferIBL; }
    bool preferPBRSurface() const { return preferences_.preferPBRSurface; }
    DocumentSessionKind kind() const { return kind_; }
    bool allowsDrawingCommands() const { return kind_ == DocumentSessionKind::Draft; }

private:
    std::unique_ptr<mulan::io::Document> document_;
    DocumentRenderPreferences preferences_;
    DocumentSessionKind kind_ = DocumentSessionKind::Draft;
};
