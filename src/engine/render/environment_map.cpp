/**
 * @file environment_map.cpp
 * @brief IBL 烘焙管线实现（equirect 2D 简化版）
 * @author hxxcxx
 * @date 2026-07-05
 */

#include "environment_map.h"

#include "shader/shader_loader.h"
#include "fullscreen/fullscreen_blit.h"
#include "../rhi/render_state.h"
#include "../rhi/sampler.h"
#include "../rhi/buffer.h"
#include "../rhi/command_list.h"
#include "../rhi/bind_group.h"
#include "../rhi/pipeline_state.h"

#include <mulan/core/image/image.h>

#include <cstdio>

namespace mulan::engine {

// IBL 参数 UBO 布局（与 ibl_*.frag.slang cbuffer IBLParams 一致）
struct IBLParamsGPU {
    uint32_t sampleCount = 64;
    float roughness = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

namespace {

// 创建一个 fullscreen-blit PSO：用 ibl.vert + 指定 frag，目标 format
std::unique_ptr<PipelineState> createBakePSO(RHIDevice& device, Shader* vs, Shader* fs, TextureFormat colorFmt) {
    GraphicsPipelineDesc desc{};
    desc.name = "IBL_Bake";
    desc.vs = vs;
    desc.ps = fs;
    desc.topology = PrimitiveTopology::TriangleList;
    desc.cullMode = CullMode::None;
    desc.fillMode = FillMode::Solid;
    desc.depthStencil.depthEnable = false;

    using PB = PipelineBinding;
    uint8_t bc = 0;
    desc.descriptorBindings[bc++] = { 0, 1, DescriptorType::UniformBuffer, PB::kStageFragment };
    desc.descriptorBindings[bc++] = { 1, 1, DescriptorType::TextureSRV, PB::kStageFragment };
    desc.descriptorBindings[bc++] = { 2, 1, DescriptorType::Sampler, PB::kStageFragment };
    desc.descriptorBindingCount = bc;

    desc.colorFormats[0] = colorFmt;
    desc.colorTargetCount = 1;
    desc.depthEnable = false;

    auto r = device.createPipelineState(desc);
    if (!r) {
        std::fprintf(stderr, "[IBL] createPSO failed: %s\n", r.error().message.c_str());
        return nullptr;
    }
    return std::move(*r);
}

}  // namespace

IBLPipeline::~IBLPipeline() = default;

bool IBLPipeline::bake(RHIDevice& device, const std::string& hdrPath) {
    // 1. 加载 equirect HDR → RGBA32F 2D 纹理
    auto image = mulan::core::FloatImage::loadHDRExpected(hdrPath, 4);
    if (!image || !(*image)->valid()) {
        const char* reason = image ? "invalid HDR image" : image.error().message.c_str();
        std::fprintf(stderr, "[IBL] Failed to load HDR: %s (%s)\n", hdrPath.c_str(), reason);
        return false;
    }

    TextureDesc eqDesc;
    eqDesc.name = "IBL_SourceEquirect";
    eqDesc.format = TextureFormat::RGBA32_Float;
    eqDesc.dimension = TextureDimension::Texture2D;
    eqDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    eqDesc.width = (*image)->width();
    eqDesc.height = (*image)->height();
    auto eqR = device.createTexture(eqDesc);
    if (!eqR) {
        std::fprintf(stderr, "[IBL] createTexture(source) failed\n");
        return false;
    }
    auto sourceEquirect = std::move(*eqR);
    device.uploadTextureData(
            sourceEquirect.get(),
            TextureUploadDesc::tightlyPacked(std::span((*image)->data(), (*image)->totalBytes() / sizeof(float)),
                                             (*image)->width(), (*image)->height(), TextureFormat::RGBA32_Float));

    // 2. 创建三张输出纹理（2D equirect 表示）
    auto make2D = [](RHIDevice& dev, uint32_t width, uint32_t height, TextureFormat fmt,
                     const char* name) -> std::unique_ptr<Texture> {
        TextureDesc d;
        d.name = name;
        d.format = fmt;
        d.dimension = TextureDimension::Texture2D;
        d.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::RenderTarget;
        d.width = width;
        d.height = height;
        d.depth = 1;
        auto r = dev.createTexture(d);
        if (!r) {
            std::fprintf(stderr, "[IBL] createTexture(%s) failed\n", name);
            return nullptr;
        }
        return std::move(*r);
    };

    irradiance_ = make2D(device, kIrradianceW, kIrradianceH, TextureFormat::RGBA16_Float, "IBL_Irradiance");
    prefilter_ = make2D(device, kPrefilterW, kPrefilterH, TextureFormat::RGBA16_Float, "IBL_Prefilter");
    brdf_lut_ = make2D(device, kBrdfLUTSize, kBrdfLUTSize, TextureFormat::RG16_Float, "IBL_BRDFLUT");
    if (!irradiance_ || !prefilter_ || !brdf_lut_)
        return false;

    // 3. sampler + UBO
    auto sampR = device.createSampler(SamplerDesc::linearClamp());
    if (!sampR)
        return false;
    linear_sampler_ = std::move(*sampR);

    auto uboR = device.createBuffer(BufferDesc::uniform(sizeof(IBLParamsGPU), "IBL_ParamsUBO"));
    if (!uboR)
        return false;
    auto paramUBO = std::move(*uboR);

    // 4. 加载 VS + 3 个 frag
    auto vs = loadShader(device, ShaderType::Vertex, "ibl.vert");
    if (!vs) {
        std::fprintf(stderr, "[IBL] load vs failed\n");
        return false;
    }
    auto irradianceFS = loadShader(device, ShaderType::Pixel, "ibl_irradiance.frag");
    auto prefilterFS = loadShader(device, ShaderType::Pixel, "ibl_prefilter.frag");
    auto brdfLutFS = loadShader(device, ShaderType::Pixel, "ibl_brdf_lut.frag");
    if (!irradianceFS || !prefilterFS || !brdfLutFS) {
        std::fprintf(stderr, "[IBL] load frag shaders failed\n");
        return false;
    }

    // 5. 一次 cmd list 录制全部 3 张烘焙，统一提交 + waitIdle
    auto cmdR = device.createCommandList();
    if (!cmdR)
        return false;
    CommandList* cmd = cmdR->get();
    cmd->begin();

    // 5a. irradiance
    {
        auto pso = createBakePSO(device, vs->get(), irradianceFS->get(), TextureFormat::RGBA16_Float);
        if (!pso)
            return false;
        BindGroupDesc bgd;
        bgd.addUBO(0, paramUBO.get(), 0, sizeof(IBLParamsGPU));
        bgd.addTexture(1, sourceEquirect.get());
        bgd.addSampler(2, linear_sampler_.get());
        auto bgR = device.createBindGroup(pso->bindGroupLayout(), bgd);
        if (!bgR)
            return false;
        IBLParamsGPU params;
        params.sampleCount = 64;
        cmd->updateBuffer(paramUBO.get(), 0, sizeof(params), &params);
        blitToSlice(*cmd, *pso, **bgR, *irradiance_, TextureFormat::RGBA16_Float, 0, 0, kIrradianceW, kIrradianceH,
                    true);
    }
    // 5b. prefilter（单档 roughness=0.5）
    {
        auto pso = createBakePSO(device, vs->get(), prefilterFS->get(), TextureFormat::RGBA16_Float);
        if (!pso)
            return false;
        BindGroupDesc bgd;
        bgd.addUBO(0, paramUBO.get(), 0, sizeof(IBLParamsGPU));
        bgd.addTexture(1, sourceEquirect.get());
        bgd.addSampler(2, linear_sampler_.get());
        auto bgR = device.createBindGroup(pso->bindGroupLayout(), bgd);
        if (!bgR)
            return false;
        IBLParamsGPU params;
        params.sampleCount = 64;
        params.roughness = 0.5f;
        cmd->updateBuffer(paramUBO.get(), 0, sizeof(params), &params);
        blitToSlice(*cmd, *pso, **bgR, *prefilter_, TextureFormat::RGBA16_Float, 0, 0, kPrefilterW, kPrefilterH, true);
    }
    // 5c. BRDF LUT（不采样外部纹理，但 binding 1/2 仍需占位）
    {
        auto pso = createBakePSO(device, vs->get(), brdfLutFS->get(), TextureFormat::RG16_Float);
        if (!pso)
            return false;
        BindGroupDesc bgd;
        bgd.addUBO(0, paramUBO.get(), 0, sizeof(IBLParamsGPU));
        bgd.addTexture(1, sourceEquirect.get());   // 占位，shader 不采样
        bgd.addSampler(2, linear_sampler_.get());  // 占位
        auto bgR = device.createBindGroup(pso->bindGroupLayout(), bgd);
        if (!bgR)
            return false;
        IBLParamsGPU params;
        params.sampleCount = 512;
        cmd->updateBuffer(paramUBO.get(), 0, sizeof(params), &params);
        blitToSlice(*cmd, *pso, **bgR, *brdf_lut_, TextureFormat::RG16_Float, 0, 0, kBrdfLUTSize, kBrdfLUTSize, true);
    }

    cmd->end();
    device.executeCommandList(cmd);
    device.waitIdle();

    sourceEquirect.reset();  // 释放源 equirect

    std::fprintf(stderr, "[IBL] bake OK (irradiance=%ux%u prefilter=%ux%u lut=%ux%u)\n", kIrradianceW, kIrradianceH,
                 kPrefilterW, kPrefilterH, kBrdfLUTSize, kBrdfLUTSize);
    return true;
}

}  // namespace mulan::engine
