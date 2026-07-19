#pragma once

#include "../services/recent_files_store.h"

#include <QWidget>

class QGridLayout;
class QIcon;
class QLabel;
class QSize;

class StartupPage final : public QWidget {
    Q_OBJECT
public:
    explicit StartupPage(QWidget* parent = nullptr);

    void setRecentFiles(QList<RecentFileEntry> recentFiles);

signals:
    void newDocumentRequested();
    void openDocumentRequested();
    void recentFileRequested(const QString& filePath);
    void recentFileMissing(const QString& filePath);

private:
    void rebuildItems();
    void activateRecentFile(const QString& filePath);
    QIcon recentFileIcon(const RecentFileEntry& entry, const QSize& size) const;
    QString recentFileTooltip(const RecentFileEntry& entry) const;

    QList<RecentFileEntry> recent_files_;
    QWidget* content_ = nullptr;
    QWidget* recent_tiles_ = nullptr;
    QGridLayout* recent_tiles_layout_ = nullptr;
    QLabel* empty_state_ = nullptr;
};
