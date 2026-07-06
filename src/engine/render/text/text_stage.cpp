#include "text_stage.h"

#include "text_layout.h"
#include "../frame/render_frame.h"
#include "../gpu_scene_contract.h"
#include "../shader/shader_loader.h"
#include "../../engine_error_code.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"

#include <mulan/graphics/vertex/vertex_layout.h>

#include <algorithm>
#include <cstdio>

namespace mulan::engine {

namespace {

constexpr uint32_t kInitialTextVertices = 4096;
constexpr uint32_t kInitialTextIndices = 6144;

bool fileExists(const char* path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

}  // namespace

TextStage::TextStage(RHIDevice& device) : device_(&device) {
}

TextStage::~TextStage() = default;

core::Result<void> TextStage::init(RHIDevice& device, const RenderTargetInfo& target) {
    device_ = &device;

    if (!loadShaders()) {
        return std::unexpected(makeError(EngineErrorCode::ShaderFileNotFound, "TextStage shader load failed"));
    }
    if (!createPipeline(target)) {
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed, "TextStage PSO creation failed"));
    }

    auto params = device_->createBuffer(BufferDesc::uniform(sizeof(TextParamsGPU), "Text_ParamsUBO"));
    if (!params) {
        return std::unexpected(params.error());
    }
    params_ubo_ = std::move(*params);

    if (!createBuffers(kInitialTextVertices, kInitialTextIndices)) {
        return std::unexpected(makeError(EngineErrorCode::BufferCreateFailed, "TextStage buffer creation failed"));
    }

    if (loadDefaultFont()) {
        createBindGroup();
    } else {
        std::fprintf(stderr, "[TextStage] default font load failed; text draws will be skipped\n");
    }

    initialized_ = true;
    return {};
}

void TextStage::shutdown(RHIDevice&) {
    bind_group_.reset();
    index_buffer_.reset();
    vertex_buffer_.reset();
    params_ubo_.reset();
    default_font_.reset();
    pso_.reset();
    fs_.reset();
    vs_.reset();
    items_.clear();
    vertices_.clear();
    indices_.clear();
    vertex_capacity_ = 0;
    index_capacity_ = 0;
    initialized_ = false;
}

void TextStage::beginFrame(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    clear();
}

void TextStage::clear() {
    items_.clear();
}

void TextStage::addText(const TextDrawDesc& desc) {
    if (!desc.text.empty()) {
        items_.push_back(desc);
    }
}

bool TextStage::loadShaders() {
    auto vs = loadShader(*device_, ShaderType::Vertex, "text.vert");
    if (!vs) {
        std::fprintf(stderr, "[TextStage] vertex shader: %s\n", vs.error().message.c_str());
        return false;
    }
    vs_ = std::move(*vs);

    auto fs = loadShader(*device_, ShaderType::Pixel, "text.frag");
    if (!fs) {
        std::fprintf(stderr, "[TextStage] fragment shader: %s\n", fs.error().message.c_str());
        return false;
    }
    fs_ = std::move(*fs);
    return true;
}

bool TextStage::createPipeline(const RenderTargetInfo& target) {
    using PB = PipelineBinding;

    GraphicsPipelineDesc desc;
    desc.name = "TextStage";
    desc.vs = vs_.get();
    desc.ps = fs_.get();
    desc.vertexLayout = graphics::layouts::text();
    desc.topology = PrimitiveTopology::TriangleList;
    desc.cullMode = CullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite = false;
    desc.depthStencil.depthFunc = CompareFunc::Always;
    desc.blend.renderTargets[0].blendEnable = true;
    desc.blend.renderTargets[0].srcBlend = BlendFactor::SrcAlpha;
    desc.blend.renderTargets[0].dstBlend = BlendFactor::InvSrcAlpha;
    desc.blend.renderTargets[0].blendOp = BlendOp::Add;
    desc.blend.renderTargets[0].srcBlendAlpha = BlendFactor::One;
    desc.blend.renderTargets[0].dstBlendAlpha = BlendFactor::InvSrcAlpha;
    desc.blend.renderTargets[0].blendOpAlpha = BlendOp::Add;
    desc.colorFormats[0] = target.colorFormat;
    desc.colorTargetCount = 1;
    desc.depthStencilFormat = target.depthFormat;
    desc.depthEnable = target.hasDepth;
    desc.sampleCount = target.sampleCount;
    desc.descriptorBindings[0] = {
        .binding = 0, .count = 1, .type = DescriptorType::UniformBuffer, .stages = PB::kStageVertex | PB::kStageFragment
    };
    desc.descriptorBindings[1] = {
        .binding = 1, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
    };
    desc.descriptorBindings[2] = {
        .binding = 2, .count = 1, .type = DescriptorType::Sampler, .stages = PB::kStageFragment
    };
    desc.descriptorBindingCount = 3;

    auto pso = device_->createPipelineState(desc);
    if (!pso) {
        std::fprintf(stderr, "[TextStage] createPipelineState: %s\n", pso.error().message.c_str());
        return false;
    }
    pso_ = std::move(*pso);
    return true;
}

bool TextStage::createBuffers(uint32_t vertexCapacity, uint32_t indexCapacity) {
    BufferDesc vb;
    vb.name = "Text_DynamicVB";
    vb.size = static_cast<uint32_t>(sizeof(TextVertex) * vertexCapacity);
    vb.usage = BufferUsage::Dynamic;
    vb.bindFlags = BufferBindFlags::VertexBuffer;
    auto vertex = device_->createBuffer(vb);
    if (!vertex) {
        std::fprintf(stderr, "[TextStage] vertex buffer: %s\n", vertex.error().message.c_str());
        return false;
    }

    BufferDesc ib;
    ib.name = "Text_DynamicIB";
    ib.size = static_cast<uint32_t>(sizeof(uint32_t) * indexCapacity);
    ib.usage = BufferUsage::Dynamic;
    ib.bindFlags = BufferBindFlags::IndexBuffer;
    auto index = device_->createBuffer(ib);
    if (!index) {
        std::fprintf(stderr, "[TextStage] index buffer: %s\n", index.error().message.c_str());
        return false;
    }

    vertex_buffer_ = std::move(*vertex);
    index_buffer_ = std::move(*index);
    vertex_capacity_ = vertexCapacity;
    index_capacity_ = indexCapacity;
    return true;
}

bool TextStage::ensureCapacity(uint32_t vertexCount, uint32_t indexCount) {
    if (vertexCount <= vertex_capacity_ && indexCount <= index_capacity_) {
        return true;
    }

    const uint32_t newVertexCapacity = std::max(vertexCount, vertex_capacity_ * 2u);
    const uint32_t newIndexCapacity = std::max(indexCount, index_capacity_ * 2u);
    return createBuffers(newVertexCapacity, newIndexCapacity);
}

bool TextStage::loadDefaultFont() {
    static constexpr const char* kCandidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/SFNS.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    };

    for (const char* path : kCandidates) {
        if (!path || !fileExists(path)) {
            continue;
        }
        auto font = std::make_unique<FontAtlas>(device_);
        if (font->load(path, 48.0f, 1024, 1024)) {
            default_font_ = std::move(font);
            return true;
        }
    }
    return false;
}

bool TextStage::createBindGroup() {
    if (!pso_ || !params_ubo_ || !default_font_ || !default_font_->atlasTexture()) {
        return false;
    }

    if (!default_font_->atlasSampler() && device_->backend() != GraphicsBackend::D3D12) {
        std::fprintf(stderr, "[TextStage] atlas sampler is required by this backend\n");
        return false;
    }

    BindGroupDesc bg;
    bg.addUBO(0, params_ubo_.get(), 0, sizeof(TextParamsGPU));
    bg.addTexture(1, default_font_->atlasTexture());
    if (default_font_->atlasSampler()) {
        bg.addSampler(2, default_font_->atlasSampler());
    }

    auto bindGroup = device_->createBindGroup(pso_->bindGroupLayout(), bg);
    if (!bindGroup) {
        std::fprintf(stderr, "[TextStage] createBindGroup: %s\n", bindGroup.error().message.c_str());
        return false;
    }
    bind_group_ = std::move(*bindGroup);
    return true;
}

void TextStage::buildGeometry() {
    vertices_.clear();
    indices_.clear();

    if (!default_font_ || !default_font_->isLoaded()) {
        return;
    }

    for (const TextDrawDesc& item : items_) {
        if (item.space != TextSpace::Screen) {
            continue;
        }

        const float fontSize = std::max(item.sizePx, 1.0f);
        float x = static_cast<float>(item.positionPx.x);
        float y = static_cast<float>(item.positionPx.y);
        const float textWidth = TextLayout::measureWidth(*default_font_, item.text, fontSize);
        const float textHeight = fontSize;

        switch (item.anchor) {
        case TextAnchor::TopLeft: break;
        case TextAnchor::Center:
            x -= textWidth * 0.5f;
            y -= textHeight * 0.5f;
            break;
        case TextAnchor::CenterLeft: y -= textHeight * 0.5f; break;
        case TextAnchor::CenterRight:
            x -= textWidth;
            y -= textHeight * 0.5f;
            break;
        }

        const float color[4] = { clamp01(static_cast<float>(item.color.x)), clamp01(static_cast<float>(item.color.y)),
                                 clamp01(static_cast<float>(item.color.z)), clamp01(static_cast<float>(item.color.w)) };
        TextLayout::layout(*default_font_, item.text, x, y, fontSize, color, vertices_, indices_);
    }
}

void TextStage::updateParams() {
    TextParamsGPU params{};
    const bool vulkanLike = device_->backend() == GraphicsBackend::Vulkan;
    const math::Mat4 ortho =
            vulkanLike
                    ? math::Mat4::ortho(0.0, static_cast<double>(width_), 0.0, static_cast<double>(height_), -1.0, 1.0)
                    : math::Mat4::ortho(0.0, static_cast<double>(width_), static_cast<double>(height_), 0.0, -1.0, 1.0);
    const math::Mat4 projection = device_->clipSpaceCorrectionMatrix() * ortho;
    storeGpuMat4(params.orthoProjection, projection);
    params.bgColor[0] = 0.0f;
    params.bgColor[1] = 0.0f;
    params.bgColor[2] = 0.0f;
    params.bgColor[3] = 0.0f;
    params.pxRange = default_font_ ? default_font_->pxRange() : 4.0f;
    params_ubo_->update(0, sizeof(TextParamsGPU), &params);
}

void TextStage::execute(RenderFrame& frame) {
    if (!initialized_ || !pso_ || !bind_group_ || !vertex_buffer_ || !index_buffer_ || items_.empty()) {
        return;
    }

    width_ = frame.view.width;
    height_ = frame.view.height;
    buildGeometry();
    if (vertices_.empty() || indices_.empty()) {
        return;
    }
    if (!ensureCapacity(static_cast<uint32_t>(vertices_.size()), static_cast<uint32_t>(indices_.size()))) {
        return;
    }

    updateParams();
    vertex_buffer_->update(0, static_cast<uint32_t>(sizeof(TextVertex) * vertices_.size()), vertices_.data());
    index_buffer_->update(0, static_cast<uint32_t>(sizeof(uint32_t) * indices_.size()), indices_.data());

    auto& cmd = frame.cmd;
    cmd.setViewport({ 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f });
    cmd.setScissorRect({ 0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_) });
    cmd.setPipelineState(pso_.get());
    cmd.bindGroup(*bind_group_);
    cmd.setVertexBuffer(0, vertex_buffer_.get());
    cmd.setIndexBuffer(index_buffer_.get());
    cmd.drawIndexed(DrawIndexedAttribs{ static_cast<uint32_t>(indices_.size()), 1, 0 });
}

}  // namespace mulan::engine
