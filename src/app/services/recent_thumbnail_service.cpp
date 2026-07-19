#include "recent_thumbnail_service.h"

#include "../ui/document_viewport.h"
#include "../ui/recent_thumbnail_spec.h"

#include <mulan/view/capture/capture_image_encoder.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {

QImage frameToContent(const QImage& image) {
    if (image.isNull() || image.width() < 2 || image.height() < 2) {
        return image;
    }

    const QColor background = image.pixelColor(0, 0);
    QRect contentBounds;
    constexpr int kBackgroundTolerance = 20;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int difference =
                    std::max({ std::abs(pixel.red() - background.red()), std::abs(pixel.green() - background.green()),
                               std::abs(pixel.blue() - background.blue()) });
            if (pixel.alpha() > 0 && difference > kBackgroundTolerance) {
                contentBounds |= QRect(x, y, 1, 1);
            }
        }
    }
    if (contentBounds.isNull()) {
        return image;
    }

    const int padding = std::max(8, std::min(image.width(), image.height()) / 14);
    contentBounds.adjust(-padding, -padding, padding, padding);
    contentBounds = contentBounds.intersected(image.rect());

    const QImage cropped = image.copy(contentBounds);
    QImage framed(image.size(), QImage::Format_RGBA8888);
    framed.fill(background);

    QPainter painter(&framed);
    const QSize targetSize = cropped.size().scaled(framed.size(), Qt::KeepAspectRatio);
    const QRect targetRect((framed.width() - targetSize.width()) / 2, (framed.height() - targetSize.height()) / 2,
                           targetSize.width(), targetSize.height());
    painter.drawImage(targetRect, cropped);
    return framed;
}

bool savePngAtomically(const QImage& image, const QString& path) {
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

}  // namespace

QString RecentThumbnailService::capture(DocumentViewport& viewport, const QString& documentPath) const {
    if (documentPath.isEmpty() || !viewport.isReady()) {
        return {};
    }

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (cacheRoot.isEmpty()) {
        return {};
    }
    const QString directory = QDir(cacheRoot).filePath("recent-thumbnails");
    if (!QDir().mkpath(directory)) {
        return {};
    }

    const QString absolutePath = QFileInfo(documentPath).absoluteFilePath();
    const QByteArray key = QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString thumbnailPath = QDir(directory).filePath(QString::fromLatin1(key) + ".png");

    mulan::view::CaptureRequest request;
    request.name = "recent-thumbnail";
    request.desc.width = recent_thumbnail::kCaptureWidth;
    request.desc.height = recent_thumbnail::kCaptureHeight;
    request.desc.readback = true;
    request.visual.style = mulan::view::CaptureRenderStyle::ShadedWithEdges;
    request.visual.showViewCube = false;
    request.visual.showOverlays = false;

    auto captured = viewport.capture(std::move(request));
    if (!captured) {
        return {};
    }
    auto decoded = mulan::view::CaptureImageEncoder::toImage(captured->result);
    if (!decoded) {
        return {};
    }
    const std::shared_ptr<mulan::core::Image> rgba = (*decoded)->toRGBA();
    if (!rgba || !rgba->valid()) {
        return {};
    }

    const QImage pixels(rgba->data(), static_cast<int>(rgba->width()), static_cast<int>(rgba->height()),
                        static_cast<qsizetype>(rgba->rowBytes()), QImage::Format_RGBA8888);
    const QImage framed = frameToContent(pixels.copy());
    return savePngAtomically(framed, thumbnailPath) ? thumbnailPath : QString{};
}
