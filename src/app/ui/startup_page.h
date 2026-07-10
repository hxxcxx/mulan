#pragma once

#include <QDateTime>
#include <QWidget>

class QGridLayout;
class QIcon;
class QLabel;

class StartupPage final : public QWidget {
    Q_OBJECT
public:
    explicit StartupPage(QWidget* parent = nullptr);

    void recordOpenedFile(const QString& filePath);
    void removeRecentFile(const QString& filePath);
    /// 为后续离屏渲染预留：缩略图生成完成后可直接回填并持久化。
    void setRecentThumbnail(const QString& filePath, const QString& thumbnailPath);

signals:
    void newDocumentRequested();
    void openDocumentRequested();
    void recentFileRequested(const QString& filePath);

private:
    struct RecentFileEntry {
        QString path;
        QDateTime lastOpened;
        QString thumbnailPath;
    };

    void loadRecentFiles();
    void saveRecentFiles() const;
    void rebuildItems();
    void activateRecentFile(const QString& filePath);
    QIcon recentFileIcon(const RecentFileEntry& entry) const;
    QString recentFileTooltip(const RecentFileEntry& entry) const;
    static QString normalizedPath(const QString& filePath);

    QWidget* recent_tiles_ = nullptr;
    QGridLayout* recent_tiles_layout_ = nullptr;
    QLabel* empty_state_ = nullptr;
    QList<RecentFileEntry> recent_files_;
};
