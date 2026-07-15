/**
 * @file document_area.h
 * @brief 多文档标签管理区 — 封装 SARibbonTabBar + SARibbonStackedWidget + 启动页 + 文档生命周期
 * @author hxxcxx
 * @date 2026-04-23
 */
#pragma once

#include <QStackedWidget>
#include <unordered_map>

class DocWidget;
class DocumentSession;
class StartupPage;
class SARibbonStackedWidget;
class SARibbonTabBar;

class DocumentArea : public QWidget {
    Q_OBJECT
public:
    explicit DocumentArea(QWidget* parent = nullptr);
    ~DocumentArea();

    /// 添加一个文档标签，返回对应的 DocWidget
    DocWidget* addDocument(DocumentSession* session, const QString& title);

    /// 关闭当前激活的文档标签；用户取消丢弃未保存修改时返回 false。
    bool closeCurrentDocument();

    /// 关闭指定索引的标签；用户取消丢弃未保存修改时返回 false。
    bool closeDocument(int index);

    /// 在应用退出前统一确认全部 dirty 文档；确认完成后再集中关闭，避免只关闭一半。
    bool closeAllDocuments();

    /// 获取当前激活的 DocWidget（可能为 nullptr）
    DocWidget* currentDocWidget() const;

    /// 当前打开的文档数量
    int documentCount() const;

    void recordOpenedFile(const QString& filePath);
    void removeRecentFile(const QString& filePath);
    void setRecentThumbnail(const QString& filePath, const QString& thumbnailPath);

signals:
    /// 文档切换，name 为文档显示名，空表示切到了欢迎页
    void currentDocumentChanged(const QString& name);

    /// 文档被打开
    void documentOpened(const QString& name);

    /// 文档被关闭
    void documentClosed();

    /// 文档即将关闭；此时视图和会话仍有效，可同步保存最终视角的缩略图。
    void documentClosing(DocWidget* document, const QString& filePath);

    /// 当前文档的编辑状态变化，需要刷新命令可用性
    void currentDocumentCommandStateInvalidated();
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

    // DocWidget 到 DocumentSession 的映射，管理会话生命周期。
    std::unordered_map<DocWidget*, DocumentSession*> docs_;
};
