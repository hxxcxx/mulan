/**
 * @file texture_asset.h
 * @brief TextureAsset 保存文档层贴图源信息。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "asset.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

class TextureAsset : public Asset {
public:
    TextureAsset(AssetId id, std::string name, std::string sourcePath = {})
        : Asset(id, AssetKind::Texture, std::move(name)), source_path_(std::move(sourcePath)) {}

    const std::string& sourcePath() const { return source_path_; }
    void setSourcePath(std::string path) { source_path_ = std::move(path); }

    /// 内嵌编码字节（PNG/JPG/...，未解码）。非空时优先于 sourcePath，
    /// 用于 GLB bufferView / data: URI / LoadExternalImages 已加载到内存的图像。
    /// 注：sRGB 不再存于 TextureAsset，而由使用方（material slot）决定。
    const std::vector<std::byte>& embeddedBytes() const { return embedded_bytes_; }
    void setEmbeddedBytes(std::vector<std::byte> bytes) { embedded_bytes_ = std::move(bytes); }
    bool hasEmbeddedData() const { return !embedded_bytes_.empty(); }

    /// MIME 类型提示（"image/png" 等），可空——让 ImageLoader 自检测
    const std::string& mimeType() const { return mime_type_; }
    void setMimeType(std::string mt) { mime_type_ = std::move(mt); }

    int width() const { return width_; }
    int height() const { return height_; }
    void setSize(int width, int height) {
        width_ = width;
        height_ = height;
    }

private:
    std::string source_path_;
    std::vector<std::byte> embedded_bytes_;
    std::string mime_type_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace mulan::asset
