/**
 * @file document_area.h
 * @brief 多文档标签管理区 — 封装 SARibbonTabBar + SARibbonStackedWidget + 启动页 + 文档生命周期
 * @author hxxcxx
 * @date 2026-04-23 (原始) / 2026-07-15 (文档会话所有权收口)
 */
#pragma once

#include <QStackedWidget>

#include <memory>
#include <unordered_map>

namespace mulan::editor {
class DocumentSession;
}
namespace mulan::view {
struct ViewConfig;
}

class DocumentViewport;
class StartupPage;
class SARibbonStackedWidget;
class SARibbonTabBar;

class DocumentArea : public QWidget {
    Q_OBJECT
public:
    explicit DocumentArea(QWidget* parent = nullptr);
    ~DocumentArea();

    /// 接管文档会话的唯一所有权并添加标签；初始化失败时销毁会话并返回 nullptr。
    DocumentViewport* addDocument(std::unique_ptr<mulan::editor::DocumentSession> session, const QString& title,
                                  const mulan::view::ViewConfig& viewConfig);

    /// 关闭指定索引的标签；用户取消丢弃未保存修改时返回 false。
    bool closeDocument(int index);

    /// 在应用退出前统一确认需要保护的 dirty 文档；草稿当前没有保存流程，直接关闭。
    bool closeAllDocuments();

    /// 获取当前激活的文档视口（可能为 nullptr）。
    DocumentViewport* currentViewport() const;

    void recordOpenedFile(const QString& filePath);
    void setRecentThumbnail(const QString& filePath, const QString& thumbnailPath);

signals:
    /// 文档切换，name 为文档显示名，空表示切到了欢迎页
    void currentDocumentChanged(const QString& name);

    /// 文档即将关闭；此时视图和会话仍有效，可同步保存最终视角的缩略图。
    void documentClosing(DocumentViewport* viewport, const QString& filePath);

    /// 当前文档的编辑状态变化，需要刷新命令可用性
    void currentDocumentCommandStateInvalidated();
    void currentDocumentRuntimeFailed(const QString& message);
    void startupNewRequested();
    void startupOpenRequested();
    void startupRecentFileRequested(const QString& filePath);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);

private:
    bool confirmDiscard(int index);
    bool closeDocumentUnchecked(int index);

    QStackedWidget* stack_ = nullptr;
    QWidget* document_page_ = nullptr;
    SARibbonTabBar* document_tab_bar_ = nullptr;
    SARibbonStackedWidget* document_stack_ = nullptr;
    StartupPage* startup_page_ = nullptr;

    // DocumentArea 唯一持有会话；DocumentViewport/DocumentView 只借用指针，销毁会话前必须先 shutdown。
    std::unordered_map<DocumentViewport*, std::unique_ptr<mulan::editor::DocumentSession>> sessions_by_viewport_;
};
