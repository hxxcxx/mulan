/**
 * @file surface_pipeline_provider.h
 * @brief 定义材质表面管线请求及其设备侧解析边界。
 * @author hxxcxx
 * @date 2026-07-20
 */

#pragma once

#include <mulan/graphics/material_types.h>

#include <cstdint>

namespace mulan::engine {

class PipelineState;

enum class SurfacePipelineFamily : uint8_t {
    Unlit,
    UnlitTangent,
    Legacy,
    LegacyTangent,
    PBR,
    PBRTangent,
};

inline SurfacePipelineFamily surfacePipelineFamily(graphics::MaterialShadingModel model, bool tangentLayout) {
    switch (model) {
    case graphics::MaterialShadingModel::Unlit:
        return tangentLayout ? SurfacePipelineFamily::UnlitTangent : SurfacePipelineFamily::Unlit;
    case graphics::MaterialShadingModel::Lambert:
    case graphics::MaterialShadingModel::BlinnPhong:
        return tangentLayout ? SurfacePipelineFamily::LegacyTangent : SurfacePipelineFamily::Legacy;
    case graphics::MaterialShadingModel::MetallicRoughness:
        return tangentLayout ? SurfacePipelineFamily::PBRTangent : SurfacePipelineFamily::PBR;
    }
    return SurfacePipelineFamily::PBR;
}

struct SurfacePipelineRequest {
    SurfacePipelineFamily family = SurfacePipelineFamily::PBR;
    graphics::AlphaMode alphaMode = graphics::AlphaMode::Opaque;
    bool doubleSided = false;
    bool reverseWinding = false;
};

class SurfacePipelineProvider {
public:
    virtual ~SurfacePipelineProvider() = default;
    virtual PipelineState* acquireSurfacePipeline(const SurfacePipelineRequest& request) = 0;
};

}  // namespace mulan::engine
