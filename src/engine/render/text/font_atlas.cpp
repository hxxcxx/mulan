#include "font_atlas.h"

#include <msdfgen.h>
#include <msdfgen-ext.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>

#include <mulan/core/log/log.h>

#include <cstdio>
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace mulan::engine {

namespace {

constexpr std::array<uint32_t, 105> kDefaultCharset = [] {
    std::array<uint32_t, 105> chars{};
    uint32_t index = 0;
    for (uint32_t c = 32; c < 127; ++c) {
        chars[index++] = c;
    }
    for (uint32_t c : { 0x3001u, 0x3002u, 0xFF0Cu, 0xFF0Eu, 0xFF08u, 0xFF09u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du }) {
        chars[index++] = c;
    }
    return chars;
}();

uint64_t fnv1a64(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct FreetypeDeleter {
    void operator()(msdfgen::FreetypeHandle* handle) const {
        if (handle) {
            msdfgen::deinitializeFreetype(handle);
        }
    }
};

struct FontDeleter {
    void operator()(msdfgen::FontHandle* font) const {
        if (font) {
            msdfgen::destroyFont(font);
        }
    }
};

}  // namespace

// ============================================================
// 构造 / 析构
// ============================================================

FontAtlas::FontAtlas(RHIDevice* device) : device_(device) {
}

FontAtlas::~FontAtlas() = default;

// ============================================================
// 加载字体并生成 MSDF 图集
// ============================================================

bool FontAtlas::load(const char* fontPath, float fontSize, uint32_t atlasWidth, uint32_t atlasHeight) {
    FontAtlasCpuData data;
    if (!generateCpuData(fontPath, fontSize, atlasWidth, atlasHeight, data)) {
        return false;
    }
    return loadFromCpuData(std::move(data));
}

bool FontAtlas::generateCpuData(const char* fontPath, float fontSize, uint32_t atlasWidth, uint32_t atlasHeight,
                                FontAtlasCpuData& outData) {
    // 预检：文件是否存在
    FILE* testFile = nullptr;
#ifdef _WIN32
    fopen_s(&testFile, fontPath, "rb");
#else
    testFile = fopen(fontPath, "rb");
#endif
    if (!testFile) {
        LOG_ERROR("[FontAtlas] Font file not found: {}", fontPath);
        return false;
    }
    fclose(testFile);

    outData = {};
    outData.baseFontSize = fontSize;
    outData.atlasWidth = atlasWidth;
    outData.atlasHeight = atlasHeight;
    outData.charsetHash = defaultCharsetHash();

    // --- 1. 初始化 FreeType ---
    std::unique_ptr<msdfgen::FreetypeHandle, FreetypeDeleter> ft(msdfgen::initializeFreetype());
    if (!ft) {
        LOG_ERROR("[FontAtlas] FreeType initialization failed");
        return false;
    }

    // --- 2. 加载字体 ---
    std::unique_ptr<msdfgen::FontHandle, FontDeleter> font(msdfgen::loadFont(ft.get(), fontPath));
    if (!font) {
        LOG_ERROR("[FontAtlas] Failed to load font: {}", fontPath);
        return false;
    }

    // --- 3. 配置字符集 ---
    std::vector<msdf_atlas::GlyphGeometry> glyphs;
    msdf_atlas::FontGeometry fontGeometry(&glyphs);

    // ASCII 可打印字符 (32~126)
    msdf_atlas::Charset charset;
    for (uint32_t c : kDefaultCharset) {
        charset.add(c);
    }

    int loaded = fontGeometry.loadCharset(font.get(), 1.0, charset);
    LOG_INFO("[FontAtlas] Loaded {} glyphs from {}", loaded, fontPath);

    // --- 4. MSDF 边缘着色 ---
    const double maxCornerAngle = 3.0;
    for (auto& glyph : glyphs)
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);

    // --- 5. 紧凑打包 ---
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensions((int) atlasWidth, (int) atlasHeight);
    packer.setScale((double) fontSize);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(glyphs.data(), (int) glyphs.size());

    // 获取实际尺寸和像素范围
    int packW = 0, packH = 0;
    packer.getDimensions(packW, packH);
    outData.atlasWidth = (uint32_t) std::max(packW, 1);
    outData.atlasHeight = (uint32_t) std::max(packH, 1);
    outData.pxRange = (float) (packer.getPixelRange().upper - packer.getPixelRange().lower);

    // --- 6. 生成 MSDF 位图 ---
    using AtlasGenerator = msdf_atlas::ImmediateAtlasGenerator<float, 3, msdf_atlas::msdfGenerator,
                                                               msdf_atlas::BitmapAtlasStorage<msdfgen::byte, 3>>;

    AtlasGenerator generator((int) outData.atlasWidth, (int) outData.atlasHeight);
    msdf_atlas::GeneratorAttributes attr;
    generator.setAttributes(attr);
    generator.setThreadCount(4);
    generator.generate(glyphs.data(), (int) glyphs.size());

    // --- 7. 获取位图数据并转换为 RGBA8 ---
    msdfgen::BitmapConstRef<msdfgen::byte, 3> atlasBitmap =
            (msdfgen::BitmapConstRef<msdfgen::byte, 3>) generator.atlasStorage();

    outData.rgbaPixels.resize(outData.atlasWidth * outData.atlasHeight * 4);
    for (uint32_t y = 0; y < outData.atlasHeight; ++y) {
        for (uint32_t x = 0; x < outData.atlasWidth; ++x) {
            // atlasBitmap 使用 Y-up 坐标，需要翻转为 Y-down (屏幕空间)
            int srcY = (int) (outData.atlasHeight - 1 - y);
            const msdfgen::byte* pixel = atlasBitmap((int) x, srcY);
            uint32_t idx = (y * outData.atlasWidth + x) * 4;
            outData.rgbaPixels[idx + 0] = pixel[0];
            outData.rgbaPixels[idx + 1] = pixel[1];
            outData.rgbaPixels[idx + 2] = pixel[2];
            outData.rgbaPixels[idx + 3] = 0xFF;  // Alpha 全部 255
        }
    }

    // --- 8. 提取字形信息 ---
    for (const auto& glyph : glyphs) {
        if (glyph.isWhitespace())
            continue;

        GlyphInfo info;
        info.unicode = glyph.getCodepoint();
        info.advanceX = static_cast<float>(glyph.getAdvance() * outData.baseFontSize);

        // 平面边界（字形相对基线的位置）
        double pl, pb, pr, pt;
        glyph.getQuadPlaneBounds(pl, pb, pr, pt);
        // planeBounds: Y-up 坐标，转换为 Y-down
        // pl=left, pb=bottom(up), pr=right, pt=top(up)
        // 在 Y-down 屏幕空间：top 是向下的
        info.planeLeft = static_cast<float>(pl * outData.baseFontSize);
        info.planeTop = static_cast<float>(pt * outData.baseFontSize);  // 基线到字形顶部的偏移（Y-up 正值）
        info.width = static_cast<float>((pr - pl) * outData.baseFontSize);
        info.height = static_cast<float>((pt - pb) * outData.baseFontSize);

        // Atlas 中的 UV 边界
        double al, ab, ar, at;
        glyph.getQuadAtlasBounds(al, ab, ar, at);
        // atlasBounds 是 Y-up，需要翻转
        info.atlasU = (float) (al / (double) outData.atlasWidth);
        info.atlasV = (float) (1.0 - at / (double) outData.atlasHeight);   // 翻转 Y
        info.atlasU2 = (float) (ar / (double) outData.atlasWidth);
        info.atlasV2 = (float) (1.0 - ab / (double) outData.atlasHeight);  // 翻转 Y

        outData.glyphs[info.unicode] = info;
    }

    return true;
}

bool FontAtlas::loadFromCpuData(FontAtlasCpuData data) {
    const uint64_t expectedBytes =
            static_cast<uint64_t>(data.atlasWidth) * static_cast<uint64_t>(data.atlasHeight) * 4u;
    if (data.atlasWidth == 0 || data.atlasHeight == 0 || data.rgbaPixels.size() != expectedBytes ||
        data.glyphs.empty()) {
        LOG_ERROR("[FontAtlas] Invalid CPU atlas data");
        return false;
    }

    base_font_size_ = data.baseFontSize;
    atlas_width_ = data.atlasWidth;
    atlas_height_ = data.atlasHeight;
    px_range_ = data.pxRange;
    glyphs_ = std::move(data.glyphs);

    // --- 1. 上传 GPU 纹理 ---
    bool uploaded = uploadAtlas(data.rgbaPixels);

    // --- 2. 创建采样器 ---
    if (uploaded) {
        SamplerDesc samplerDesc;
        samplerDesc.minFilter = SamplerFilter::Linear;
        samplerDesc.magFilter = SamplerFilter::Linear;
        samplerDesc.addressU = SamplerAddressMode::ClampToEdge;
        samplerDesc.addressV = SamplerAddressMode::ClampToEdge;
        samplerDesc.debugName = "MSDF_Atlas_Sampler";
        auto sampler = device_->createSampler(samplerDesc);
        if (!sampler) {
            LOG_ERROR("[FontAtlas] Atlas sampler creation failed: {}", sampler.error().message);
        } else {
            sampler_ = std::move(*sampler);
        }
    }

    return uploaded;
}

uint64_t FontAtlas::defaultCharsetHash() {
    uint64_t hash = fnv1a64("mulan-default-msdf-charset-v1", 29);
    for (uint32_t c : kDefaultCharset) {
        hash = fnv1a64(&c, sizeof(c), hash);
    }
    return hash;
}

// ============================================================
// 查找字形
// ============================================================

const GlyphInfo* FontAtlas::getGlyph(uint32_t unicode) const {
    auto it = glyphs_.find(unicode);
    return it != glyphs_.end() ? &it->second : nullptr;
}

// ============================================================
// 上传图集纹理到 GPU
// ============================================================

bool FontAtlas::uploadAtlas(const std::vector<uint8_t>& rgbaData) {
    if (!device_ || rgbaData.empty() || atlas_width_ == 0 || atlas_height_ == 0) {
        LOG_ERROR("[FontAtlas] Invalid atlas upload request");
        return false;
    }

    TextureDesc texDesc;
    texDesc.name = "MSDF_Atlas";
    texDesc.format = TextureFormat::RGBA8_UNorm;
    texDesc.dimension = TextureDimension::Texture2D;
    texDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst;
    texDesc.width = atlas_width_;
    texDesc.height = atlas_height_;
    texDesc.depth = 1;
    texDesc.mipLevels = 1;
    texDesc.arraySize = 1;

    auto result = device_->createTexture(texDesc);
    if (!result) {
        LOG_ERROR("[FontAtlas] Atlas texture creation failed: {}", result.error().message);
        return false;
    }
    texture_ = std::move(*result);

    // 当前引擎的 TextureDesc 没有 initData 字段，纹理内容通过 RHIDevice 的统一上传入口写入。
    // 后端相关的 UploadContext 与布局转换细节留在 RHI 层，FontAtlas 不直接依赖具体后端。
    auto uploadResult = device_->uploadTextureData(
            texture_.get(), TextureUploadDesc::tightlyPacked(std::span(rgbaData), atlas_width_, atlas_height_,
                                                             TextureFormat::RGBA8_UNorm));
    if (!uploadResult) {
        LOG_ERROR("[FontAtlas] Atlas texture upload failed: {}", uploadResult.error().message);
        texture_.reset();
        return false;
    }

    return true;
}

}  // namespace mulan::engine
