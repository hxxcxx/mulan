/**
 * @file editor_tool.cpp
 * @brief 编辑工具上下文实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "editor_tool.h"

#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/view/view_context.h>

#include <string>
#include <utility>

namespace mulan::app {

void ToolContext::clearPreview() {
    if (view_) {
        view_->clearPreview();
    }
}

void ToolContext::setPreview(std::vector<asset::CurvePrimitive> primitives) {
    if (view_) {
        view_->previewLayer().setCurves(std::move(primitives));
    }
}

bool ToolContext::createCurve(std::string_view name, asset::CurvePrimitive primitive) {
    if (!session_ || !session_->document()) {
        return false;
    }

    io::DocumentEditor editor(*session_->document());
    const auto created = editor.createCurve(std::string(name), std::move(primitive));
    if (!created) {
        return false;
    }

    if (binding_) {
        binding_->refresh();
    }
    return true;
}

}  // namespace mulan::app
