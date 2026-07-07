/**
 * @file editor_tool.h
 * @brief 定义视图编辑工具的生命周期和输入响应接口。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "editor_input.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/view/preview_layer.h>

#include <string_view>
#include <vector>

class DocumentSession;
class DocumentViewBinding;

namespace mulan::view {
class ViewContext;
}

namespace mulan::app {

class ToolContext {
public:
    ToolContext(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding)
        : session_(session), view_(view), binding_(binding) {}

    DocumentSession* session() const { return session_; }
    view::ViewContext* view() const { return view_; }
    DocumentViewBinding* binding() const { return binding_; }

    void clearPreview();
    void setPreview(std::vector<asset::CurvePrimitive> primitives);
    bool createCurve(std::string_view name, asset::CurvePrimitive primitive);

private:
    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
};

enum class ToolInputResult {
    Ignored,
    Consumed,
    Finished,
    Cancelled,
};

enum class ToolFinishReason {
    Finished,
    Cancelled,
    Replaced,
};

class EditorTool {
public:
    virtual ~EditorTool() = default;

    virtual std::string_view id() const = 0;
    virtual void begin(ToolContext& context) { (void) context; }
    virtual ToolInputResult handleInput(ToolContext& context, const EditorInput& input) = 0;
    virtual void end(ToolContext& context, ToolFinishReason reason) {
        (void) context;
        (void) reason;
    }
};

}  // namespace mulan::app
