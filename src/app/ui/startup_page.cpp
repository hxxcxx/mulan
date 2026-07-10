#include "startup_page.h"

#include <QFileInfo>
#include <QDir>
#include <QGridLayout>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kMaximumRecentFiles = 10;
constexpr auto kSettingsOrganization = "mulan";
constexpr auto kSettingsApplication = "MuLan";

QString readableFileSize(qint64 bytes) {
    static constexpr const char* suffixes[] = { "B", "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes);
    int suffix = 0;
    while (value >= 1024.0 && suffix < 4) {
        value /= 1024.0;
        ++suffix;
    }
    return suffix == 0 ? QStringLiteral("%1 B").arg(bytes)
                       : QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10.0 ? 1 : 0).arg(suffixes[suffix]);
}

}  // namespace

StartupPage::StartupPage(QWidget* parent) : QWidget(parent) {
    setObjectName("startupPage");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    auto* content = new QWidget(this);
    content->setObjectName("startupContent");
    content->setMaximumWidth(1280);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(44, 34, 44, 30);
    layout->setSpacing(0);

    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(18);
    auto* logo = new QLabel(content);
    logo->setFixedSize(72, 72);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(
            QPixmap(":/app/branding/app-icon.png").scaled(68, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brandRow->addWidget(logo);

    auto* brandText = new QVBoxLayout();
    brandText->setSpacing(3);
    auto* title = new QLabel(tr("MuLan"), content);
    title->setObjectName("startupTitle");
    auto* subtitle = new QLabel(tr("Geometry modeling and visualization"), content);
    subtitle->setObjectName("startupSubtitle");
    brandText->addStretch();
    brandText->addWidget(title);
    brandText->addWidget(subtitle);
    brandText->addStretch();
    brandRow->addLayout(brandText);
    brandRow->addStretch();
    layout->addLayout(brandRow);
    layout->addSpacing(30);

    auto* actionTitle = new QLabel(tr("Start"), content);
    actionTitle->setObjectName("sectionTitle");
    layout->addWidget(actionTitle);
    layout->addSpacing(12);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(12);
    auto makeActionButton = [content](const QString& text, const QString& iconPath) {
        auto* button = new QToolButton(content);
        button->setText(text);
        button->setIcon(QIcon(iconPath));
        button->setProperty("uiRole", "startupAction");
        button->setIconSize(QSize(28, 28));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedSize(180, 52);
        return button;
    };
    auto* newButton = makeActionButton(tr("New document"), ":/app/icons/icon/file-new.svg");
    auto* openButton = makeActionButton(tr("Open files"), ":/app/icons/icon/file-open.svg");
    connect(newButton, &QToolButton::clicked, this, &StartupPage::newDocumentRequested);
    connect(openButton, &QToolButton::clicked, this, &StartupPage::openDocumentRequested);
    actionRow->addWidget(newButton);
    actionRow->addWidget(openButton);
    actionRow->addStretch();
    layout->addLayout(actionRow);
    layout->addSpacing(28);

    auto* recentTitle = new QLabel(tr("Recent files"), content);
    recentTitle->setObjectName("sectionTitle");
    layout->addWidget(recentTitle);
    layout->addSpacing(12);

    recent_tiles_ = new QWidget(content);
    recent_tiles_->setObjectName("recentFileTiles");
    recent_tiles_layout_ = new QGridLayout(recent_tiles_);
    recent_tiles_layout_->setContentsMargins(0, 0, 0, 0);
    recent_tiles_layout_->setHorizontalSpacing(12);
    recent_tiles_layout_->setVerticalSpacing(16);
    recent_tiles_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Keep the first two recent-file columns exactly aligned with the two
    // 180px start actions above (with the same 12px gutter).
    recent_tiles_layout_->setColumnMinimumWidth(0, 180);
    recent_tiles_layout_->setColumnMinimumWidth(1, 180);
    layout->addWidget(recent_tiles_, 1);

    empty_state_ = new QLabel(tr("No recent files yet. Open a model to see it here."), recent_tiles_);
    empty_state_->setObjectName("emptyState");
    empty_state_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    empty_state_->setAttribute(Qt::WA_TransparentForMouseEvents);

    pageLayout->addStretch();
    pageLayout->addWidget(content, 1);
    pageLayout->addStretch();

    loadRecentFiles();
    rebuildItems();
}

QString StartupPage::normalizedPath(const QString& filePath) {
    QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

void StartupPage::recordOpenedFile(const QString& filePath) {
    const QString path = normalizedPath(filePath);
    if (path.isEmpty())
        return;

    QString thumbnailPath;
    for (auto it = recent_files_.begin(); it != recent_files_.end();) {
        if (normalizedPath(it->path).compare(path, Qt::CaseInsensitive) == 0) {
            thumbnailPath = it->thumbnailPath;
            it = recent_files_.erase(it);
        } else {
            ++it;
        }
    }
    recent_files_.prepend({ path, QDateTime::currentDateTime(), thumbnailPath });
    while (recent_files_.size() > kMaximumRecentFiles)
        recent_files_.removeLast();
    saveRecentFiles();
    rebuildItems();
}

void StartupPage::removeRecentFile(const QString& filePath) {
    const QString path = normalizedPath(filePath);
    for (auto it = recent_files_.begin(); it != recent_files_.end();) {
        if (normalizedPath(it->path).compare(path, Qt::CaseInsensitive) == 0)
            it = recent_files_.erase(it);
        else
            ++it;
    }
    saveRecentFiles();
    rebuildItems();
}

void StartupPage::setRecentThumbnail(const QString& filePath, const QString& thumbnailPath) {
    const QString path = normalizedPath(filePath);
    for (RecentFileEntry& entry : recent_files_) {
        if (normalizedPath(entry.path).compare(path, Qt::CaseInsensitive) == 0) {
            entry.thumbnailPath = thumbnailPath;
            saveRecentFiles();
            rebuildItems();
            return;
        }
    }
}

void StartupPage::activateRecentFile(const QString& path) {
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Recent File Missing"),
                             tr("The file no longer exists and will be removed from Recent Files:\n%1").arg(path));
        removeRecentFile(path);
        return;
    }
    emit recentFileRequested(path);
}

void StartupPage::loadRecentFiles() {
    // Do not rely on QApplication's implicit identity here: it can differ
    // between a debug executable and an installed build, making recents look
    // as if they vanished after restarting the application.
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    const int count = settings.beginReadArray("recentFiles");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RecentFileEntry entry;
        entry.path = settings.value("path").toString();
        entry.lastOpened = settings.value("lastOpened").toDateTime();
        entry.thumbnailPath = settings.value("thumbnailPath").toString();
        if (!entry.path.isEmpty())
            recent_files_.append(std::move(entry));
    }
    settings.endArray();
}

void StartupPage::saveRecentFiles() const {
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    settings.beginWriteArray("recentFiles");
    for (int i = 0; i < recent_files_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("path", recent_files_[i].path);
        settings.setValue("lastOpened", recent_files_[i].lastOpened);
        settings.setValue("thumbnailPath", recent_files_[i].thumbnailPath);
    }
    settings.endArray();
    settings.sync();
}

QIcon StartupPage::recentFileIcon(const RecentFileEntry& entry) const {
    if (!entry.thumbnailPath.isEmpty() && QFileInfo::exists(entry.thumbnailPath)) {
        const QPixmap thumbnail(entry.thumbnailPath);
        if (!thumbnail.isNull())
            return QIcon(thumbnail);
    }
    // TODO: 离屏渲染完成后由 setRecentThumbnail() 回填；当前按文件类型显示占位图。
    const QString suffix = QFileInfo(entry.path).suffix().toLower();
    if (suffix == "step" || suffix == "stp" || suffix == "iges" || suffix == "igs")
        return QIcon(":/app/icons/icon/view-cube.svg");
    return QIcon(":/app/icons/icon/view-wireframe.svg");
}

QString StartupPage::recentFileTooltip(const RecentFileEntry& entry) const {
    const QFileInfo info(entry.path);
    const QString created = info.birthTime().isValid() ? info.birthTime().toString("yyyy/M/d HH:mm") : tr("Unknown");
    const QString modified =
            info.lastModified().isValid() ? info.lastModified().toString("yyyy/M/d HH:mm") : tr("Unknown");
    const QString opened = entry.lastOpened.isValid() ? entry.lastOpened.toString("yyyy/M/d HH:mm") : tr("Unknown");
    return QString("<b>%1</b><br>%2<br><br>%3: %4<br>%5: %6<br>%7: %8<br>%9: %10")
            .arg(info.fileName().toHtmlEscaped(), QDir::toNativeSeparators(entry.path).toHtmlEscaped(), tr("Size"),
                 readableFileSize(info.size()), tr("Created"), created, tr("Modified"), modified, tr("Opened"), opened);
}

void StartupPage::rebuildItems() {
    while (QLayoutItem* item = recent_tiles_layout_->takeAt(0)) {
        QWidget* widget = item->widget();
        if (widget != empty_state_)
            delete widget;
        delete item;
    }

    empty_state_->hide();

    constexpr int kRecentColumns = 4;
    for (int index = 0; index < recent_files_.size(); ++index) {
        const RecentFileEntry& entry = recent_files_[index];
        const QFileInfo info(entry.path);
        auto* tile = new QToolButton(recent_tiles_);
        tile->setProperty("uiRole", "recentFile");
        tile->setAutoRaise(true);
        tile->setFixedSize(180, 132);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIcon(recentFileIcon(entry));
        tile->setIconSize(QSize(180, 101));
        tile->setText(info.fileName());
        tile->setToolTip(recentFileTooltip(entry));
        connect(tile, &QToolButton::clicked, this, [this, path = entry.path]() { activateRecentFile(path); });
        recent_tiles_layout_->addWidget(tile, index / kRecentColumns, index % kRecentColumns);
    }

    if (recent_files_.isEmpty()) {
        recent_tiles_layout_->addWidget(empty_state_, 0, 0, Qt::AlignLeft | Qt::AlignTop);
        empty_state_->show();
    }
}
