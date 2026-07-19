#include "startup_page.h"
#include "recent_thumbnail_spec.h"

#include <QFileInfo>
#include <QDir>
#include <QGridLayout>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QFrame>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kContentMaximumWidth = 1420;
constexpr int kContentHorizontalMargin = 32;
constexpr int kRecentTileWidth = recent_thumbnail::kDisplayWidth;
constexpr int kRecentThumbnailHeight = recent_thumbnail::kDisplayHeight;
constexpr int kRecentTileHeight = 160;
constexpr int kRecentTileSpacing = 12;

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

QPixmap cropRecentThumbnail(const QPixmap& source, const QSize& targetSize) {
    if (source.isNull())
        return {};
    const QPixmap scaled = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - targetSize.width()) / 2);
    const int y = std::max(0, (scaled.height() - targetSize.height()) / 2);
    return scaled.copy(x, y, targetSize.width(), targetSize.height());
}

}  // namespace

StartupPage::StartupPage(QWidget* parent) : QWidget(parent) {
    setObjectName("startupPage");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    content_ = new QWidget(this);
    content_->setObjectName("startupContent");
    content_->setMaximumWidth(kContentMaximumWidth);
    auto* layout = new QHBoxLayout(content_);
    layout->setContentsMargins(kContentHorizontalMargin, 28, kContentHorizontalMargin, 28);
    layout->setSpacing(28);

    auto* launchPanel = new QWidget(content_);
    launchPanel->setObjectName("startupLaunchPanel");
    launchPanel->setFixedWidth(300);
    auto* launchLayout = new QVBoxLayout(launchPanel);
    launchLayout->setContentsMargins(28, 18, 28, 20);
    launchLayout->setSpacing(0);

    auto* logo = new QLabel(content_);
    logo->setFixedSize(82, 82);
    logo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    logo->setPixmap(
            QPixmap(":/app/branding/app-icon.png").scaled(78, 78, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto* title = new QLabel(tr("MuLan"), content_);
    title->setObjectName("startupTitle");
    auto* subtitle = new QLabel(tr("Geometry modeling and visualization"), content_);
    subtitle->setObjectName("startupSubtitle");
    subtitle->setWordWrap(true);
    launchLayout->addWidget(logo);
    launchLayout->addSpacing(16);
    launchLayout->addWidget(title);
    launchLayout->addSpacing(4);
    launchLayout->addWidget(subtitle);
    launchLayout->addSpacing(64);

    auto* actionTitle = new QLabel(tr("START"), content_);
    actionTitle->setObjectName("startupSectionEyebrow");
    launchLayout->addWidget(actionTitle);
    launchLayout->addSpacing(12);

    auto makeActionButton = [this](const QString& text, const QString& iconPath) {
        auto* button = new QToolButton(content_);
        button->setText(text);
        button->setIcon(QIcon(iconPath));
        button->setProperty("uiRole", "startupAction");
        button->setIconSize(QSize(34, 34));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedSize(244, 72);
        return button;
    };
    auto* newButton = makeActionButton(tr("New document"), ":/app/icons/icon/file-new.svg");
    auto* openButton = makeActionButton(tr("Open files"), ":/app/icons/icon/file-open.svg");
    connect(newButton, &QToolButton::clicked, this, &StartupPage::newDocumentRequested);
    connect(openButton, &QToolButton::clicked, this, &StartupPage::openDocumentRequested);
    launchLayout->addWidget(newButton);
    launchLayout->addSpacing(14);
    launchLayout->addWidget(openButton);
    launchLayout->addStretch();
    layout->addWidget(launchPanel);

    auto* divider = new QFrame(content_);
    divider->setObjectName("startupDivider");
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedHeight(640);
    layout->addWidget(divider, 0, Qt::AlignTop);

    auto* galleryPanel = new QWidget(content_);
    auto* galleryLayout = new QVBoxLayout(galleryPanel);
    galleryLayout->setContentsMargins(0, 18, 0, 0);
    galleryLayout->setSpacing(14);

    auto* recentTitle = new QLabel(tr("Recent files"), content_);
    recentTitle->setObjectName("sectionTitle");
    galleryLayout->addWidget(recentTitle);

    recent_tiles_ = new QWidget(content_);
    recent_tiles_->setObjectName("recentFileTiles");
    recent_tiles_layout_ = new QGridLayout(recent_tiles_);
    recent_tiles_layout_->setContentsMargins(0, 0, 0, 0);
    recent_tiles_layout_->setHorizontalSpacing(kRecentTileSpacing);
    recent_tiles_layout_->setVerticalSpacing(16);
    recent_tiles_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    galleryLayout->addWidget(recent_tiles_, 1);
    layout->addWidget(galleryPanel, 1);

    empty_state_ = new QLabel(tr("No recent files yet. Open a model to see it here."), recent_tiles_);
    empty_state_->setObjectName("emptyState");
    empty_state_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    empty_state_->setAttribute(Qt::WA_TransparentForMouseEvents);

    pageLayout->addStretch();
    pageLayout->addWidget(content_, 1);
    pageLayout->addStretch();

    rebuildItems();
}

void StartupPage::setRecentFiles(QList<RecentFileEntry> recentFiles) {
    recent_files_ = std::move(recentFiles);
    rebuildItems();
}

void StartupPage::activateRecentFile(const QString& path) {
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Recent File Missing"),
                             tr("The file no longer exists and will be removed from Recent Files:\n%1").arg(path));
        emit recentFileMissing(path);
        return;
    }
    emit recentFileRequested(path);
}

QIcon StartupPage::recentFileIcon(const RecentFileEntry& entry, const QSize& size) const {
    if (!entry.thumbnailPath.isEmpty() && QFileInfo::exists(entry.thumbnailPath)) {
        const QPixmap thumbnail(entry.thumbnailPath);
        if (!thumbnail.isNull())
            return QIcon(cropRecentThumbnail(thumbnail, size));
    }
    // 尚未生成缩略图时，按文件类型显示稳定的占位图标。
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

    for (int index = 0; index < recent_files_.size(); ++index) {
        const RecentFileEntry& entry = recent_files_[index];
        const QFileInfo info(entry.path);
        auto* tile = new QToolButton(recent_tiles_);
        const bool featured = index == 0;
        tile->setProperty("uiRole", featured ? "recentFeatured" : "recentFile");
        tile->setAutoRaise(true);
        const QSize imageSize = featured ? QSize(360, 300) : QSize(kRecentTileWidth, kRecentThumbnailHeight);
        tile->setFixedSize(featured ? QSize(360, 331) : QSize(kRecentTileWidth, kRecentTileHeight));
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIcon(recentFileIcon(entry, imageSize));
        tile->setIconSize(imageSize);
        tile->setText(info.fileName());
        tile->setToolTip(recentFileTooltip(entry));
        connect(tile, &QToolButton::clicked, this, [this, path = entry.path]() { activateRecentFile(path); });
        if (featured) {
            recent_tiles_layout_->addWidget(tile, 0, 0, 3, 1, Qt::AlignTop);
        } else {
            const int compactIndex = index - 1;
            recent_tiles_layout_->addWidget(tile, compactIndex / 3, 1 + compactIndex % 3, Qt::AlignTop);
        }
    }

    if (recent_files_.isEmpty()) {
        recent_tiles_layout_->addWidget(empty_state_, 0, 0, Qt::AlignLeft | Qt::AlignTop);
        empty_state_->show();
    }
}
