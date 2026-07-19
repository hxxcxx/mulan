#include "recent_files_store.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <utility>

namespace {

constexpr int kMaximumRecentFiles = 10;
constexpr auto kSettingsOrganization = "mulan";
constexpr auto kSettingsApplication = "MuLan";

Qt::CaseSensitivity pathCaseSensitivity() {
#if defined(Q_OS_WIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

}  // namespace

RecentFilesStore::RecentFilesStore() {
    load();
}

QString RecentFilesStore::normalizedPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return {};
    }
    const QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool RecentFilesStore::samePath(const QString& lhs, const QString& rhs) {
    return normalizedPath(lhs).compare(normalizedPath(rhs), pathCaseSensitivity()) == 0;
}

void RecentFilesStore::recordOpenedFile(const QString& filePath) {
    const QString path = normalizedPath(filePath);
    if (path.isEmpty()) {
        return;
    }

    QString thumbnailPath;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (samePath(it->path, path)) {
            thumbnailPath = it->thumbnailPath;
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    entries_.prepend(RecentFileEntry{ path, QDateTime::currentDateTime(), std::move(thumbnailPath) });
    while (entries_.size() > kMaximumRecentFiles) {
        entries_.removeLast();
    }
    save();
}

void RecentFilesStore::removeFile(const QString& filePath) {
    const QString path = normalizedPath(filePath);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (samePath(it->path, path)) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    save();
}

void RecentFilesStore::setThumbnail(const QString& filePath, const QString& thumbnailPath) {
    const QString path = normalizedPath(filePath);
    for (RecentFileEntry& entry : entries_) {
        if (samePath(entry.path, path)) {
            entry.thumbnailPath = thumbnailPath;
            save();
            return;
        }
    }
}

void RecentFilesStore::load() {
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    const int count = settings.beginReadArray("recentFiles");
    for (int index = 0; index < count && entries_.size() < kMaximumRecentFiles; ++index) {
        settings.setArrayIndex(index);
        RecentFileEntry entry{
            .path = normalizedPath(settings.value("path").toString()),
            .lastOpened = settings.value("lastOpened").toDateTime(),
            .thumbnailPath = settings.value("thumbnailPath").toString(),
        };
        if (!entry.path.isEmpty()) {
            entries_.append(std::move(entry));
        }
    }
    settings.endArray();
}

void RecentFilesStore::save() const {
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    settings.beginWriteArray("recentFiles", entries_.size());
    for (int index = 0; index < entries_.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue("path", entries_[index].path);
        settings.setValue("lastOpened", entries_[index].lastOpened);
        settings.setValue("thumbnailPath", entries_[index].thumbnailPath);
    }
    settings.endArray();
    settings.sync();
}
