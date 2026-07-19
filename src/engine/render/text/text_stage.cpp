#include "text_stage.h"

#include "text_layout.h"
#include "../frame/render_frame.h"
#include "../gpu_scene_contract.h"
#include "../shader/shader_loader.h"
#include "../../rhi/engine_error_code.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"

#include <mulan/core/log/log.h>
#include <mulan/graphics/vertex/vertex_layout.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

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

bool matrixExactlyEqual(const math::Mat4& lhs, const math::Mat4& rhs) {
    for (size_t column = 0; column < 4; ++column) {
        if (lhs[static_cast<int>(column)] != rhs[static_cast<int>(column)])
            return false;
    }
    return true;
}

bool textDrawExactlyEqual(const TextDrawDesc& lhs, const TextDrawDesc& rhs) {
    return lhs.text == rhs.text && lhs.font == rhs.font && lhs.space == rhs.space && lhs.anchor == rhs.anchor &&
           lhs.depthMode == rhs.depthMode && lhs.positionPx == rhs.positionPx &&
           lhs.positionWorld == rhs.positionWorld && lhs.rightWorld == rhs.rightWorld && lhs.upWorld == rhs.upWorld &&
           matrixExactlyEqual(lhs.clipFromWorld, rhs.clipFromWorld) && lhs.viewportOriginPx == rhs.viewportOriginPx &&
           lhs.viewportSizePx == rhs.viewportSizePx && lhs.sizePx == rhs.sizePx && lhs.sizeWorld == rhs.sizeWorld &&
           lhs.color == rhs.color;
}

bool textDrawListsExactlyEqual(const std::vector<TextDrawDesc>& lhs, const std::vector<TextDrawDesc>& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](const auto& a, const auto& b) { return textDrawExactlyEqual(a, b); });
}

bool projectToViewport(const math::Mat4& clipFromWorld, const math::Point2& viewportOrigin,
                       const math::Vec2& viewportSize, const math::Point3& world, math::Point2& out) {
    math::Vec4 clip = clipFromWorld * math::Vec4(world.x, world.y, world.z, 1.0);
    if (std::abs(clip.w) < 1.0e-8) {
        return false;
    }

    clip /= clip.w;
    out.x = viewportOrigin.x + (clip.x * 0.5 + 0.5) * viewportSize.x;
    out.y = viewportOrigin.y + (1.0 - (clip.y * 0.5 + 0.5)) * viewportSize.y;
    return true;
}

bool applyWorldPlanarBasis(std::vector<TextVertex>& vertices, std::vector<uint32_t>& indices, uint32_t firstVertex,
                           uint32_t firstIndex, const TextDrawDesc& item) {
    const math::Vec3 right = item.rightWorld.normalizedOr(math::Vec3::unitX());
    const math::Vec3 up = item.upWorld.normalizedOr(math::Vec3::unitY());
    const double worldPerPixel = static_cast<double>(item.sizeWorld) / std::max(static_cast<double>(item.sizePx), 1.0);

    for (uint32_t i = firstVertex; i < vertices.size(); ++i) {
        const double localX = static_cast<double>(vertices[i].x);
        const double localY = static_cast<double>(vertices[i].y);
        const math::Point3 world =
                item.positionWorld + right * (localX * worldPerPixel) - up * (localY * worldPerPixel);

        math::Point2 screen;
        if (!projectToViewport(item.clipFromWorld, item.viewportOriginPx, item.viewportSizePx, world, screen)) {
            vertices.resize(firstVertex);
            indices.resize(firstIndex);
            return false;
        }

        vertices[i].x = static_cast<float>(screen.x);
        vertices[i].y = static_cast<float>(screen.y);
    }
    return true;
}

void alignLocalTextBounds(std::vector<TextVertex>& vertices, uint32_t firstVertex, TextAnchor anchor) {
    if (firstVertex >= vertices.size()) {
        return;
    }

    float minX = vertices[firstVertex].x;
    float maxX = vertices[firstVertex].x;
    float minY = vertices[firstVertex].y;
    float maxY = vertices[firstVertex].y;
    for (uint32_t i = firstVertex + 1; i < vertices.size(); ++i) {
        minX = std::min(minX, vertices[i].x);
        maxX = std::max(maxX, vertices[i].x);
        minY = std::min(minY, vertices[i].y);
        maxY = std::max(maxY, vertices[i].y);
    }

    float dx = 0.0f;
    float dy = 0.0f;
    switch (anchor) {
    case TextAnchor::TopLeft:
        dx = -minX;
        dy = -minY;
        break;
    case TextAnchor::Center:
        dx = -(minX + maxX) * 0.5f;
        dy = -(minY + maxY) * 0.5f;
        break;
    case TextAnchor::CenterLeft:
        dx = -minX;
        dy = -(minY + maxY) * 0.5f;
        break;
    case TextAnchor::CenterRight:
        dx = -maxX;
        dy = -(minY + maxY) * 0.5f;
        break;
    }

    for (uint32_t i = firstVertex; i < vertices.size(); ++i) {
        vertices[i].x += dx;
        vertices[i].y += dy;
    }
}

}  // namespace

TextStage::TextStage(RHIDevice& device) : device_(&device) {
}

TextStage::~TextStage() = default;

ResultVoid TextStage::init(RHIDevice& device, const RenderTargetInfo& target) {
    device_ = &device;

    if (!loadShaders()) {
        return std::unexpected(makeError(EngineErrorCode::ShaderFileNotFound, "TextStage shader load failed"));
    }
    if (!createPipeline(target)) {
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed, "TextStage PSO creation failed"));
    }

    if (!createBuffers(kInitialTextVertices, kInitialTextIndices)) {
        return std::unexpected(makeError(EngineErrorCode::BufferCreateFailed, "TextStage buffer creation failed"));
    }

    font_manager_ = std::make_unique<FontManager>(device);
    if (!loadDefaultFont()) {
        LOG_WARN("[TextStage] Default font loading failed; text draws will be skipped");
    }

    initialized_ = true;
    return {};
}

void TextStage::shutdown(RHIDevice&) {
    font_bind_groups_.clear();
    index_buffer_.reset();
    vertex_buffer_.reset();
    default_font_ = nullptr;
    font_manager_.reset();
    pso_.reset();
    fs_.reset();
    vs_.reset();
    items_.clear();
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    cached_items_.clear();
    vertex_capacity_ = 0;
    index_capacity_ = 0;
    geometry_cache_valid_ = false;
    last_geometry_cache_hit_ = false;
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
        TextDrawDesc item = desc;
        if (item.depthMode == TextDepthMode::TestDepth) {
            item.depthMode = TextDepthMode::AlwaysOnTop;
        }
        items_.push_back(std::move(item));
    }
}

void TextStage::addTextList(const TextDrawList& list) {
    for (const TextDrawDesc& item : list.items()) {
        addText(item);
    }
}

bool TextStage::hasFont() const {
    return default_font_ && default_font_->isLoaded();
}

bool TextStage::hasFont(std::string_view fontKey) const {
    if (fontKey.empty() || fontKey == "default") {
        return hasFont();
    }
    if (!font_manager_) {
        return false;
    }
    FontAtlas* font = font_manager_->font(std::string(fontKey).c_str());
    return font && font->isLoaded();
}

TextMetrics TextStage::measureText(std::string_view fontKey, std::string_view text, float sizePx) const {
    const FontAtlas* font = resolveFont(fontKey);
    if (!font) {
        return {};
    }
    return TextLayout::measure(*font, text, std::max(sizePx, 1.0f));
}

bool TextStage::loadShaders() {
    auto vs = loadShader(*device_, ShaderType::Vertex, "text.vert");
    if (!vs) {
        LOG_ERROR("[TextStage] Vertex-shader loading failed: {}", vs.error().message);
        return false;
    }
    vs_ = std::move(*vs);

    auto fs = loadShader(*device_, ShaderType::Pixel, "text.frag");
    if (!fs) {
        LOG_ERROR("[TextStage] Fragment-shader loading failed: {}", fs.error().message);
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
    if (!target.hasDepth)
        desc.depthStencilFormat = TextureFormat::Unknown;
    desc.sampleCount = target.sampleCount;
    desc.descriptorBindings[0] = { .binding = 0,
                                   .count = 1,
                                   .type = DescriptorType::UniformBuffer,
                                   .stages = PB::kStageVertex | PB::kStageFragment,
                                   .mode = BindingMode::Dynamic };
    desc.descriptorBindings[1] = {
        .binding = 1, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
    };
    desc.descriptorBindings[2] = {
        .binding = 2, .count = 1, .type = DescriptorType::Sampler, .stages = PB::kStageFragment
    };
    desc.descriptorBindingCount = 3;

    auto pso = device_->createPipelineState(desc);
    if (!pso) {
        LOG_ERROR("[TextStage] Pipeline-state creation failed: {}", pso.error().message);
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
        LOG_ERROR("[TextStage] Vertex-buffer creation failed: {}", vertex.error().message);
        return false;
    }

    BufferDesc ib;
    ib.name = "Text_DynamicIB";
    ib.size = static_cast<uint32_t>(sizeof(uint32_t) * indexCapacity);
    ib.usage = BufferUsage::Dynamic;
    ib.bindFlags = BufferBindFlags::IndexBuffer;
    auto index = device_->createBuffer(ib);
    if (!index) {
        LOG_ERROR("[TextStage] Index-buffer creation failed: {}", index.error().message);
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
    if (!font_manager_) {
        return false;
    }

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
        if (font_manager_->loadFont("default", path, 48.0f, 1024)) {
            default_font_ = font_manager_->defaultFont();
            return true;
        }
    }
    return false;
}

FontAtlas* TextStage::resolveFont(std::string_view fontKey) const {
    if (!font_manager_) {
        return nullptr;
    }

    FontAtlas* font = nullptr;
    if (!fontKey.empty()) {
        font = font_manager_->font(std::string(fontKey).c_str());
    }
    if (!font) {
        font = default_font_ ? default_font_ : font_manager_->defaultFont();
    }
    return font && font->isLoaded() ? font : nullptr;
}

BindGroup* TextStage::bindGroupForFont(std::string_view fontKey, FontAtlas& font) {
    if (!pso_ || !font.atlasTexture()) {
        return nullptr;
    }

    if (!font.atlasSampler() && device_->backend() != GraphicsBackend::D3D12) {
        LOG_ERROR("[TextStage] Atlas sampler is required by the active backend");
        return nullptr;
    }

    std::string key(fontKey.empty() ? "default" : fontKey);
    auto it = font_bind_groups_.find(key);
    if (it != font_bind_groups_.end()) {
        return it->second.get();
    }

    BindGroupDesc bg;
    bg.addTexture(1, font.atlasTexture());
    if (font.atlasSampler()) {
        bg.addSampler(2, font.atlasSampler());
    }

    auto bindGroup = device_->createBindGroup(pso_->bindGroupLayout(), bg);
    if (!bindGroup) {
        LOG_ERROR("[TextStage] Bind-group creation failed: {}", bindGroup.error().message);
        return nullptr;
    }

    auto result = font_bind_groups_.emplace(std::move(key), std::move(*bindGroup));
    return result.first->second.get();
}

void TextStage::buildGeometry() {
    vertices_.clear();
    indices_.clear();
    batches_.clear();

    if (!default_font_ || !default_font_->isLoaded()) {
        return;
    }

    for (const TextDrawDesc& item : items_) {
        if (item.space != TextSpace::Screen && item.space != TextSpace::WorldPlanar) {
            continue;
        }
        FontAtlas* font = resolveFont(item.font);
        if (!font) {
            continue;
        }

        const float fontSize = std::max(item.sizePx, 1.0f);
        float x = item.space == TextSpace::Screen ? static_cast<float>(item.positionPx.x) : 0.0f;
        float y = item.space == TextSpace::Screen ? static_cast<float>(item.positionPx.y) : 0.0f;
        const float textWidth = TextLayout::measureWidth(*font, item.text, fontSize);
        const float textHeight = fontSize;

        if (item.space == TextSpace::Screen) {
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
        }

        const float color[4] = { clamp01(static_cast<float>(item.color.x)), clamp01(static_cast<float>(item.color.y)),
                                 clamp01(static_cast<float>(item.color.z)), clamp01(static_cast<float>(item.color.w)) };
        const uint32_t firstVertex = static_cast<uint32_t>(vertices_.size());
        const uint32_t firstIndex = static_cast<uint32_t>(indices_.size());
        TextLayout::layout(*font, item.text, x, y, fontSize, color, vertices_, indices_);
        if (item.space == TextSpace::WorldPlanar) {
            alignLocalTextBounds(vertices_, firstVertex, item.anchor);
            applyWorldPlanarBasis(vertices_, indices_, firstVertex, firstIndex, item);
        }
        const uint32_t indexCount = static_cast<uint32_t>(indices_.size()) - firstIndex;
        if (indexCount == 0) {
            continue;
        }
        const std::string fontKey = font == default_font_ ? std::string("default")
                                                          : (item.font.empty() ? std::string("default") : item.font);
        if (!batches_.empty() && batches_.back().font == fontKey && batches_.back().atlas == font &&
            batches_.back().firstIndex + batches_.back().indexCount == firstIndex) {
            batches_.back().indexCount += indexCount;
        } else {
            batches_.push_back(TextBatch{ fontKey, font, firstIndex, indexCount });
        }
    }
}

TextStage::TextParamsGPU TextStage::buildParams(const FontAtlas& font) const {
    TextParamsGPU params{};
    const math::Mat4 projection =
            device_->backend() == GraphicsBackend::Vulkan
                    ? math::Mat4::ortho(0.0, static_cast<double>(width_), 0.0, static_cast<double>(height_), -1.0, 1.0)
                    : math::Mat4::ortho(0.0, static_cast<double>(width_), static_cast<double>(height_), 0.0, -1.0, 1.0);
    storeGpuMat4(params.orthoProjection, projection);
    params.bgColor[0] = 0.0f;
    params.bgColor[1] = 0.0f;
    params.bgColor[2] = 0.0f;
    params.bgColor[3] = 0.0f;
    params.pxRange = font.pxRange();
    params.atlasSize[0] = static_cast<float>(font.atlasWidth());
    params.atlasSize[1] = static_cast<float>(font.atlasHeight());
    return params;
}

void TextStage::execute(RenderFrame& frame) {
    last_geometry_cache_hit_ = false;
    if (!initialized_ || !pso_ || !vertex_buffer_ || !index_buffer_ || items_.empty()) {
        return;
    }

    width_ = frame.view.width;
    height_ = frame.view.height;
    const bool reuseGeometry = geometry_cache_valid_ && cached_width_ == width_ && cached_height_ == height_ &&
                               textDrawListsExactlyEqual(items_, cached_items_);
    if (!reuseGeometry) {
        geometry_cache_valid_ = false;
        buildGeometry();
        if (vertices_.empty() || indices_.empty() || batches_.empty()) {
            return;
        }
        if (!ensureCapacity(static_cast<uint32_t>(vertices_.size()), static_cast<uint32_t>(indices_.size()))) {
            return;
        }

        const auto vertexWrite = vertex_buffer_->write(0, static_cast<uint32_t>(sizeof(TextVertex) * vertices_.size()),
                                                       vertices_.data());
        const auto indexWrite =
                index_buffer_->write(0, static_cast<uint32_t>(sizeof(uint32_t) * indices_.size()), indices_.data());
        if (!vertexWrite || !indexWrite) {
            LOG_ERROR("[TextStage] Dynamic geometry upload failed: {}",
                      !vertexWrite ? vertexWrite.error().message : indexWrite.error().message);
            return;
        }
        cached_items_ = items_;
        cached_width_ = width_;
        cached_height_ = height_;
        geometry_cache_valid_ = true;
    } else {
        last_geometry_cache_hit_ = true;
    }

    auto& cmd = frame.cmd;
    cmd.setViewport({ 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f });
    cmd.setScissorRect({ 0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_) });
    cmd.setPipelineState(pso_.get());
    cmd.setVertexBuffer(0, vertex_buffer_.get());
    cmd.setIndexBuffer(index_buffer_.get());
    for (const TextBatch& batch : batches_) {
        if (!batch.atlas) {
            continue;
        }
        BindGroup* bindGroup = bindGroupForFont(batch.font, *batch.atlas);
        if (!bindGroup) {
            continue;
        }
        const auto params = cmd.writeUniform(buildParams(*batch.atlas));
        if (!params)
            continue;
        const std::array uniforms{ DynamicUniformBinding{ 0, *params } };
        cmd.bindGroup(*bindGroup, uniforms);
        cmd.drawIndexed(DrawIndexedAttribs{ batch.indexCount, 1, batch.firstIndex });
    }
}

}  // namespace mulan::engine
