/**
 * @file DocumentArea.h
 * @brief 多文档标签管理区 — 封装 QTabWidget + 欢迎页 + 文档生命周期
 * @author hxxcxx
 * @date 2026-04-23
 */
#pragma once

#include <QStackedWidget>
#include <QTabWidget>
#include <unordered_map>

class QLabel;
class DocWidget;
class UIDocument;

class DocumentArea : public QWidget {
    Q_OBJECT
public:
    explicit DocumentArea(QWidget* parent = nullptr);
    ~DocumentArea();

    /// 添加一个文档标签，返回对应的 DocWidget
    DocWidget* addDocument(UIDocument* uiDoc, const QString& title);

    /// 关闭当前激活的文档标签
    void closeCurrentDocument();

    /// 关闭指定索引的标签
    void closeDocument(int index);

    /// 获取当前激活的 DocWidget（可能为 nullptr）
    DocWidget* currentDocWidget() const;

    /// 当前打开的文档数量
    int documentCount() const;

signals:
    /// 文档切换，name 为文档显示名，空表示切到了欢迎页
    void currentDocumentChanged(const QString& name);

    /// 文档被打开
    void documentOpened(const QString& name);

    /// 文档被关闭
    void documentClosed();

private slots:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);

private:
    QStackedWidget* m_stack     = nullptr;
    QTabWidget*     m_tabWidget = nullptr;
    QLabel*         m_welcomePage = nullptr;

    // DocWidget → UIDocument 的映射，管理 UIDocument 生命周期
    std::unordered_map<DocWidget*, UIDocument*> m_docs;
};
