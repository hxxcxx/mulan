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

namespace mulan::app {

class EditorSelectionService {
public:
    void bind(view::ViewContext* view);
    void unbind();

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
    void syncVisualState();

private:
    EditorSelectionContext context_;
    view::ViewContext* view_ = nullptr;
};

}  // namespace mulan::app
