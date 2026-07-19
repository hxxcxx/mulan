/**
 * @file recent_files_store.h
 * @brief 管理最近文件列表的规范化、排序和持久化。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct RecentFileEntry {
    QString path;
    QDateTime lastOpened;
    QString thumbnailPath;
};

class RecentFilesStore {
public:
    RecentFilesStore();

    const QList<RecentFileEntry>& entries() const { return entries_; }

    void recordOpenedFile(const QString& filePath);
    void removeFile(const QString& filePath);
    void setThumbnail(const QString& filePath, const QString& thumbnailPath);

private:
    static QString normalizedPath(const QString& filePath);
    static bool samePath(const QString& lhs, const QString& rhs);
    void load();
    void save() const;

    QList<RecentFileEntry> entries_;
};
