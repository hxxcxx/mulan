/**
 * @file transform_preview_builder.h
 * @brief TransformPreviewBuilder 根据实体变换结果生成临时预览几何。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_operation.h"
#include "draft_geometry.h"

#include <span>

namespace mulan::io {
class Document;
}

namespace mulan::app {

class TransformPreviewBuilder final {
public:
    static DraftGeometry build(const io::Document& document, std::span<const EntityTransformUpdate> updates);
};

}  // namespace mulan::app
