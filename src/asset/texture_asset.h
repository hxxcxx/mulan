/**
 * @file texture_asset.h
 * @brief TextureAsset 保存文档层贴图源信息。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "asset.h"
#include <mulan/core/image/image.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

class TextureAsset : public Asset {
public:
    TextureAsset(AssetId id, std::string name, std::string sourcePath = {})
        : Asset(id, AssetKind::Texture, std::move(name)), source_path_(std::move(sourcePath)) {}

    const std::string& sourcePath() const { return source_path_; }
    void setSourcePath(std::string path) { assignIfChanged(source_path_, std::move(path)); }

    /// Decoded image used by the render pipeline. sourcePath is only provenance.
    const std::shared_ptr<const core::Image>& image() const { return image_; }
    void setImage(std::shared_ptr<const core::Image> image) {
        int width = width_;
        int height = height_;
        if (image && image->valid()) {
            width = static_cast<int>(image->width());
            height = static_cast<int>(image->height());
        }
        if (image_ == image && width_ == width && height_ == height)
            return;

        touch();
        image_ = std::move(image);
        width_ = width;
        height_ = height;
    }
    bool hasImage() const { return image_ && image_->valid(); }

    /// Encoded source bytes (PNG/JPG/...), kept for persistence/re-export/provenance.
    /// 注：sRGB 不再存于 TextureAsset，而由使用方（material slot）决定。
    const std::vector<std::byte>& embeddedBytes() const { return embedded_bytes_; }
    void setEmbeddedBytes(std::vector<std::byte> bytes) { assignIfChanged(embedded_bytes_, std::move(bytes)); }
    bool hasEmbeddedData() const { return !embedded_bytes_.empty(); }

    /// MIME 类型提示（"image/png" 等），可空——让 ImageLoader 自检测
    const std::string& mimeType() const { return mime_type_; }
    void setMimeType(std::string mt) { assignIfChanged(mime_type_, std::move(mt)); }

    int width() const { return width_; }
    int height() const { return height_; }
    void setSize(int width, int height) {
        if (width_ == width && height_ == height)
            return;
        touch();
        width_ = width;
        height_ = height;
    }

private:
    std::string source_path_;
    std::shared_ptr<const core::Image> image_;
    std::vector<std::byte> embedded_bytes_;
    std::string mime_type_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace mulan::asset
