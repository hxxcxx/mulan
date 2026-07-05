#include "document_session.h"

#include <mulan/view/view_context.h>

DocumentSession::DocumentSession(std::unique_ptr<mulan::io::Document> doc,
                                 mulan::io::ImportReport report)
    : document_(std::move(doc))
{
    // 推导默认相机投影与 IBL 偏好：含 BREP（CAD 几何）→ 正交 + 不开 IBL（工程视图）；
    // 纯网格（glTF / OBJ / ...）→ 透视 + 允许 IBL。
    // 阈值：只要 brepAssetCount > 0 且不少于 meshAssetCount 就视为 CAD 主导。
    const bool is_cad = (report.brepAssetCount > 0)
                        && (report.meshAssetCount == 0
                            || report.brepAssetCount >= report.meshAssetCount);
    prefer_ortho_ = is_cad;
    prefer_ibl_   = !is_cad;

    syncRenderScene();

    render_scene_.forEachProxy([&](const mulan::render_scene::SceneProxy& proxy) {
        pick_id_map_[proxy.entity.index()] = proxy.entity;
    });
}

DocumentSession::~DocumentSession() {
    detachViewContext();
}

const std::string& DocumentSession::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}

void DocumentSession::syncRenderScene() {
    if (!document_ || !document_->scene() || !document_->assets()) {
        render_scene_.clear();
        return;
    }

    render_scene_.sync(*document_->scene(), *document_->assets());
}

void DocumentSession::requestRefresh() {
    syncRenderScene();

    // 重新把派生出的 RenderScene 注入视图（指针值不变，内容已更新），
    // 然后渲染一帧。若视图未 attach（如离屏尚未初始化），仅同步、不重绘。
    if (view_context_) {
        if (document_)
            view_context_->setRenderScene(&render_scene_, document_->assets());
        view_context_->renderFrame();
    }
}

void DocumentSession::attachViewContext(mulan::view::ViewContext* runtime) {
    if (view_context_) detachViewContext();
    view_context_ = runtime;

    runtime->setRenderScene(&render_scene_, document_ ? document_->assets() : nullptr);

    // 按模型类型应用相机投影模式（CAD→正交，glTF/mesh→透视），
    // 必须在 fitToBox 之前设置：正交/透视下 fitToBox 推导 distance / orthoSize 的公式不同。
    runtime->camera().setOrthographic(prefer_ortho_);

    // 按模型类型决定是否烘焙 IBL（CAD→不开，mesh→按全局开关与 HDR 存在性）。
    // enableIBL 内部幂等：重复 attach（如切回 tab）不会重烘。
    if (prefer_ibl_) {
        runtime->enableIBL();
    }

    const auto& bounds = render_scene_.sceneBounds();
    if (!bounds.isEmpty())
        runtime->camera().fitToBox(bounds);
}

void DocumentSession::detachViewContext() {
    if (view_context_) {
        view_context_->setRenderScene(nullptr, nullptr);
        view_context_ = nullptr;
    }
}

mulan::scene::EntityId DocumentSession::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::scene::EntityId::invalid();
}
