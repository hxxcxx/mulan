#include "geometry_draw_executor.h"
#include "../shader/shader_loader.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/render_types.h"
#include "../../rhi/render_state.h"
#include "../gpu_scene_contract.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

// ─── 构造 / init ───────────────────────────────────────────────

GeometryDrawExecutor::GeometryDrawExecutor(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                                           RenderTechnique technique)
    : device_(device), shared_resources_(sharedResources), technique_(TechniqueRegistry::builtin(technique)) {
}

bool GeometryDrawExecutor::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth, uint32_t sampleCount) {
    if (!loadShaders())
        return false;
    if (!createPSO(colorFmt, depthFmt, hasDepth, sampleCount))
        return false;

    if (!createFrameBindGroup(colorFmt, depthFmt, hasDepth))
        return false;

    initialized_ = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

bool GeometryDrawExecutor::loadShaders() {
    auto vs = loadShader(device_, ShaderType::Vertex, technique_.shader.vertex);
    auto fs = loadShader(device_, ShaderType::Pixel, technique_.shader.pixel);
    if (!vs) {
        return false;
    }
    if (!fs) {
        return false;
    }
    vs_ = std::move(*vs);
    fs_ = std::move(*fs);
    return true;
}

// ─── PSO ───────────────────────────────────────────────────────

bool GeometryDrawExecutor::createPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth,
                                     uint32_t sampleCount) {
    GraphicsPipelineDesc desc{};
    desc.name = technique_.debugName;
    desc.vs = vs_.get();
    desc.ps = fs_.get();
    desc.vertexLayout = technique_.vertexLayout;
    desc.topology = technique_.topology;
    desc.cullMode = CullMode::None;
    desc.frontFace = FrontFace::CounterClockwise;
    desc.fillMode = FillMode::Solid;
    desc.depthStencil.depthEnable = technique_.depthTest;
    desc.depthStencil.depthWrite = technique_.depthWrite;
    desc.depthStencil.depthFunc = technique_.depthFunc;

    using PB = PipelineBinding;
    desc.descriptorBindings[0] = {
        .binding = 0, .count = 1, .type = DescriptorType::UniformBuffer, .stages = PB::kStageVertex | PB::kStageFragment
    };
    desc.descriptorBindings[1] = {
        .binding = 1, .count = 1, .type = DescriptorType::UniformBuffer, .stages = PB::kStageVertex | PB::kStageFragment
    };
    desc.descriptorBindings[2] = {
        .binding = 2, .count = 1, .type = DescriptorType::UniformBuffer, .stages = PB::kStageFragment
    };
    uint8_t bindingCount = 3;

    // 纹理 + sampler（仅 sampleTextures=true 的 pass 声明）
    if (technique_.sampleTextures) {
        desc.descriptorBindings[bindingCount++] = {
            .binding = 3, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 4, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 5, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 6, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 7, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 8, .count = 1, .type = DescriptorType::Sampler, .stages = PB::kStageFragment
        };
        // IBL 三件套：binding 9=irradiance, 10=prefilter, 11=brdf LUT
        desc.descriptorBindings[bindingCount++] = {
            .binding = 9, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 10, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
        desc.descriptorBindings[bindingCount++] = {
            .binding = 11, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
    }
    desc.descriptorBindingCount = bindingCount;

    desc.colorFormats[0] = colorFmt;
    desc.colorTargetCount = 1;
    desc.depthStencilFormat = depthFmt;
    desc.depthEnable = hasDepth;
    desc.sampleCount = sampleCount;

    auto psoResult = device_.createPipelineState(desc);
    if (!psoResult) {
        return false;
    }
    pso_ = std::move(*psoResult);
    return true;
}

// ─── Execute ───────────────────────────────────────────────────

bool GeometryDrawExecutor::createFrameBindGroup(TextureFormat, TextureFormat, bool) {
    // 构建初始 BindGroupDesc：静态 binding（scene=0）在此绑定；
    // object=1 / material=2 offset 在每 draw 通过 updateUBO 刷新（首帧必脏）；
    // 纹理槽先用 defaultWhite 占位，每 draw 由 MeshDrawCommand::execute 更新。
    BindGroupDesc bg;
    bg.addUBO(0, shared_resources_.sceneUBO(), 0, sizeof(SceneUniforms));
    bg.addUBO(1, shared_resources_.objectUBO(), 0, MeshDrawCommand::kObjectUboStride);
    bg.addUBO(2, shared_resources_.materialUBO(), 0, 128);

    if (technique_.sampleTextures && shared_resources_.defaultWhiteTexture() && shared_resources_.defaultSampler()) {
        bg.addTexture(3, shared_resources_.defaultWhiteTexture());
        bg.addTexture(4, shared_resources_.defaultNormalTexture() ? shared_resources_.defaultNormalTexture()
                                                                  : shared_resources_.defaultWhiteTexture());
        bg.addTexture(5, shared_resources_.defaultMetallicRoughnessTexture()
                                 ? shared_resources_.defaultMetallicRoughnessTexture()
                                 : shared_resources_.defaultWhiteTexture());
        bg.addTexture(6, shared_resources_.defaultBlackTexture() ? shared_resources_.defaultBlackTexture()
                                                                 : shared_resources_.defaultWhiteTexture());
        bg.addTexture(7, shared_resources_.defaultWhiteTexture());
        bg.addSampler(8, shared_resources_.defaultSampler());
        // IBL 三件套：先用 fallback，每帧 execute 时刷新为真实烘焙产物。
        // 若 IBL fallback 未创建（创建失败），退化到 defaultWhite 以保证 descriptor 非 null
        // —— 避免 Vulkan 验证层 "descriptor never updated" 错误。
        Texture* iblFallback = shared_resources_.defaultIBLTexture() ? shared_resources_.defaultIBLTexture()
                                                                     : shared_resources_.defaultWhiteTexture();
        Texture* lutFallback = shared_resources_.defaultBrdfLUT() ? shared_resources_.defaultBrdfLUT()
                                                                  : shared_resources_.defaultWhiteTexture();
        bg.addTexture(9, iblFallback);
        bg.addTexture(10, iblFallback);
        bg.addTexture(11, lutFallback);
    }

    auto result = device_.createBindGroup(pso_->bindGroupLayout(), bg);
    if (!result) {
        std::fprintf(stderr, "[GeometryDrawExecutor] createBindGroup failed: %s\n", result.error().message.c_str());
        return false;
    }
    frame_bg_ = std::move(*result);
    return true;
}

void GeometryDrawExecutor::execute(const DrawExecutionContext& ctx) {
    if (!initialized_ || !pso_ || !ctx.cmd || !frame_bg_)
        return;

    // binding=0 (scene UBO) 内容每帧由 uploadSceneUBO 更新，但 binding 本身指向的
    // buffer/offset/range 不变 → 无需 update，复用缓存 descriptor。
    // binding=9/10/11 (IBL 三件套) 在 setIBLTextures 后生效，每帧刷新一次。
    if (technique_.sampleTextures) {
        frame_bg_->updateTexture(9, ibl_irradiance_ ? ibl_irradiance_ : shared_resources_.defaultIBLTexture());
        frame_bg_->updateTexture(10, ibl_prefilter_ ? ibl_prefilter_ : shared_resources_.defaultIBLTexture());
        frame_bg_->updateTexture(11, ibl_brdf_lut_ ? ibl_brdf_lut_ : shared_resources_.defaultBrdfLUT());
    }

    for (auto& cmd : commands_) {
        if (!cmd.visible || cmd.instanceCount == 0)
            continue;
        cmd.execute(*ctx.cmd, *frame_bg_, shared_resources_.sceneUBO(), shared_resources_.objectUBO(),
                    shared_resources_.materialUBO(), shared_resources_.defaultWhiteTexture(),
                    shared_resources_.defaultNormalTexture(), shared_resources_.defaultMetallicRoughnessTexture(),
                    shared_resources_.defaultBlackTexture(), shared_resources_.defaultSampler());
    }
}

}  // namespace mulan::engine
