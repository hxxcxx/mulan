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

private:
    std::string source_path_;
};

} // namespace mulan::asset
