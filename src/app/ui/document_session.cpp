#include "document_session.h"

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

DocumentSession::DocumentSession(std::unique_ptr<mulan::io::Document> doc, mulan::io::ImportReport report)
    : document_(std::move(doc)), preferences_(makePreferences(report)) {
}

DocumentSession::~DocumentSession() = default;

const std::string& DocumentSession::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}
