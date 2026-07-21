/**
 * @file editor_selection_service.h
 * @brief EditorSelectionService 管理编辑器选择状态并同步视图高亮。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "editor_selection.h"

#include <optional>
#include <span>

namespace mulan::view {
class ViewContext;
}

namespace mulan {
class Document;
}

namespace mulan::editor {

class EditorSelectionService {
public:
    explicit EditorSelectionService(view::ViewContext& view);
    ~EditorSelectionService();

    const EditorSelectionContext& context() const { return context_; }
    EditorSelectionContext& context() { return context_; }

    std::span<const EditorSelectionReference> selected() const { return context_.selected(); }
    bool empty() const { return context_.empty(); }

    void clear();
    void clearHover();
    void setHovered(std::optional<EditorSelectionHit> hit);
    void selectSingleAndHover(EditorSelectionHit hit);
    void clearSelectionAndHover();
    void setFilter(EditorSelectionFilter filter);
    bool pruneInvalid(const Document& document);
    void syncVisualState();

private:
    EditorSelectionContext context_;
    view::ViewContext& view_;
};

}  // namespace mulan::editor
