#include "document_session.h"

#include "core/operation/command_history.h"

#include <mulan/core/log/log.h>

#include <utility>

namespace {

DocumentRenderPreferences makePreferences(const mulan::io::ImportReport& report) {
    if (report.entityCount == 0 && report.meshAssetCount == 0 && report.brepAssetCount == 0) {
        return DocumentRenderPreferences{
            .preferOrthographic = true,
            .preferIBL = false,
            .preferPBRSurface = false,
        };
    }

    const bool isCad =
            report.brepAssetCount > 0 && (report.meshAssetCount == 0 || report.brepAssetCount >= report.meshAssetCount);
    const bool hasImportedMaterialData = report.materialCount > 0 || report.textureCount > 0;
    return DocumentRenderPreferences{
        .preferOrthographic = isCad,
        .preferIBL = false,
        .preferPBRSurface = !isCad && hasImportedMaterialData,
    };
}

}  // namespace

DocumentSession::DocumentSession(std::unique_ptr<mulan::io::Document> doc)
    : document_(std::move(doc)),
      command_history_(std::make_unique<mulan::editor::CommandHistory>()),
      preferences_(makePreferences({})),
      kind_(DocumentSessionKind::Draft) {
    LOG_INFO("[Editor] Draft document session created: name={}", displayName());
}

DocumentSession::DocumentSession(std::unique_ptr<mulan::io::Document> doc, mulan::io::ImportReport report)
    : document_(std::move(doc)),
      command_history_(std::make_unique<mulan::editor::CommandHistory>()),
      preferences_(makePreferences(report)),
      kind_(DocumentSessionKind::Imported) {
    LOG_INFO("[Editor] Imported document session created: name={}, entities={}, meshes={}, breps={}, warnings={}",
             displayName(), report.entityCount, report.meshAssetCount, report.brepAssetCount, report.warnings.size());
}

DocumentSession::~DocumentSession() {
    LOG_INFO("[Editor] Document session closed: name={}, dirty={}", displayName(),
             document_ ? document_->isDirty() : false);
}

mulan::editor::CommandHistory& DocumentSession::commandHistory() noexcept {
    return *command_history_;
}

const std::string& DocumentSession::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}
