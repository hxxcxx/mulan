#pragma once

#include "editor_tool.h"
#include "selection_target.h"

#include <mulan/asset/face_asset.h>

#include <memory>
#include <optional>

namespace mulan::io {
class Document;
}

namespace mulan::app {

/// 启动后点击 FaceAsset 或闭合曲线，再执行法线拉伸。
class SelectionExtrudeTool final : public EditorTool {
public:
    explicit SelectionExtrudeTool(const io::Document& document) : document_(&document) {}

    std::string_view id() const override { return "model.extrude"; }
    EditorAction begin() override;
    EditorAction handleInput(const EditorInput& input) override;
    EditorAction end(ToolFinishReason reason) override;

private:
    struct ExtrudeSource {
        asset::FaceDefinition profile;
        bool isFace = false;
    };

    static std::optional<ExtrudeSource> sourceFromSelection(const io::Document& document,
                                                            const EditorSelectionReference& selection);
    EditorAction selectSource(const EditorInput& input);

    EditorAction updatePreview(double signedDistance) const;
    EditorAction commit();
    double signedDistanceFor(const EditorInput& input) const;
    double profileScale() const;

    const io::Document* document_ = nullptr;
    std::optional<asset::FaceDefinition> profile_;
    bool source_is_face_ = false;
    std::optional<double> extrusion_anchor_y_;
    double signed_distance_ = 0.0;
};

}  // namespace mulan::app
