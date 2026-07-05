/**
 * @file document_session.h
 * @brief 表示一个已打开文档在界面中的运行会话。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/io/document.h>
#include <mulan/io/import_result.h>
#include <mulan/view/render_scene.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::view {
class ViewContext;
}

class DocumentSession {
public:
    /// 构造时传入 import 报告，用于推导默认相机投影模式：
    /// BREP 资产（CAD 几何） → 正交相机；纯网格（glTF 等） → 透视相机。
    /// report 可省略（默认 {}），此时保持正交（沿用 ViewContext 默认）。
    explicit DocumentSession(std::unique_ptr<mulan::io::Document> doc,
                             mulan::io::ImportReport report = {});
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    mulan::io::Document* document() { return document_.get(); }
    const mulan::io::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    void syncRenderScene();

    /// 把 Scene 的变更传播到 RenderScene 并触发一帧重绘。
    /// 在任何修改 Document 内容（如 scene()->setVisible/setGeometry/...）之后调用。
    /// 这是接通"文档变更 → 视图更新"链路的标准入口：Scene 的 EntityDirty 标记
    /// 在此被消费（当前为全量 resync，未来可改为增量）。
    void requestRefresh();

    mulan::render_scene::RenderScene& renderScene() { return render_scene_; }
    const mulan::render_scene::RenderScene& renderScene() const { return render_scene_; }

    void attachViewContext(mulan::view::ViewContext* runtime);
    void detachViewContext();

    mulan::view::ViewContext* viewContext() const { return view_context_; }

    /// 该文档建议的默认相机投影模式：true=正交（CAD/BREP），false=透视（glTF/mesh）。
    /// 在 attachViewContext 时应用到相机。
    bool preferOrthographic() const { return prefer_ortho_; }

    /// 该文档是否值得启用 IBL（环境光反射）。
    /// CAD/BREP 模型 → false（工程视图无需环境反射）；
    /// 纯 mesh（glTF/OBJ/FBX...）→ true（仍受全局开关与 HDR 文件存在性约束）。
    /// 在 attachViewContext 时按需触发烘焙。
    bool preferIBL() const { return prefer_ibl_; }

    mulan::scene::EntityId resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::io::Document> document_;
    mulan::render_scene::RenderScene render_scene_;
    mulan::view::ViewContext* view_context_ = nullptr;
    std::unordered_map<uint32_t, mulan::scene::EntityId> pick_id_map_;
    bool prefer_ortho_ = true;  // 默认正交；纯 mesh 文档构造时按 report 推翻为透视
    bool prefer_ibl_   = true;  // 默认开；CAD/BREP 文档构造时按 report 推翻为关
};
