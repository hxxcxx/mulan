/**
 * @file texture_asset.h
 * @brief TextureAsset 保存文档层贴图源信息。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "asset.h"

#include <string>
#include <utility>

namespace mulan::asset {

class TextureAsset : public Asset {
public:
    TextureAsset(AssetId id, std::string name, std::string sourcePath = {})
        : Asset(id, AssetKind::Texture, std::move(name))
        , source_path_(std::move(sourcePath)) {}

    const std::string& sourcePath() const { return source_path_; }
    void setSourcePath(std::string path) { source_path_ = std::move(path); }

    bool srgb() const { return srgb_; }
    void setSrgb(bool value) { srgb_ = value; }

    int width() const { return width_; }
    int height() const { return height_; }
    void setSize(int width, int height) {
        width_ = width;
        height_ = height;
    }

private:
    std::string source_path_;
    bool srgb_ = true;
    int width_ = 0;
    int height_ = 0;
};

} // namespace mulan::asset
