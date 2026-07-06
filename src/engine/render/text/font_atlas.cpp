#include "font_atlas.h"

#include <msdfgen.h>
#include <msdfgen-ext.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace mulan::engine {

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
    // 预检：文件是否存在
    FILE* testFile = nullptr;
#ifdef _WIN32
    fopen_s(&testFile, fontPath, "rb");
#else
    testFile = fopen(fontPath, "rb");
#endif
    if (!testFile) {
        std::fprintf(stderr, "[FontAtlas] Font file not found: %s\n", fontPath);
        return false;
    }
    fclose(testFile);

    base_font_size_ = fontSize;
    atlas_width_ = atlasWidth;
    atlas_height_ = atlasHeight;

    // --- 1. 初始化 FreeType ---
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        std::fprintf(stderr, "[FontAtlas] Failed to initialize FreeType\n");
        return false;
    }

    // --- 2. 加载字体 ---
    msdfgen::FontHandle* font = msdfgen::loadFont(ft, fontPath);
    if (!font) {
        std::fprintf(stderr, "[FontAtlas] Failed to load font: %s\n", fontPath);
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    // --- 3. 配置字符集 ---
    std::vector<msdf_atlas::GlyphGeometry> glyphs;
    msdf_atlas::FontGeometry fontGeometry(&glyphs);

    // ASCII 可打印字符 (32~126)
    msdf_atlas::Charset charset;
    for (uint32_t c = 32; c < 127; ++c)
        charset.add(c);
    // 常用中文标点
    for (auto c : { 0x3001u, 0x3002u, 0xFF0Cu, 0xFF0Eu, 0xFF08u, 0xFF09u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du })
        charset.add(c);

    int loaded = fontGeometry.loadCharset(font, 1.0, charset);
    std::fprintf(stderr, "[FontAtlas] Loaded %d glyphs from %s\n", loaded, fontPath);

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
    atlas_width_ = (uint32_t) std::max(packW, 1);
    atlas_height_ = (uint32_t) std::max(packH, 1);
    px_range_ = (float) (packer.getPixelRange().upper - packer.getPixelRange().lower);

    // --- 6. 生成 MSDF 位图 ---
    using AtlasGenerator = msdf_atlas::ImmediateAtlasGenerator<float, 3, msdf_atlas::msdfGenerator,
                                                               msdf_atlas::BitmapAtlasStorage<msdfgen::byte, 3>>;

    AtlasGenerator generator((int) atlas_width_, (int) atlas_height_);
    msdf_atlas::GeneratorAttributes attr;
    generator.setAttributes(attr);
    generator.setThreadCount(4);
    generator.generate(glyphs.data(), (int) glyphs.size());

    // --- 7. 获取位图数据并转换为 RGBA8 ---
    msdfgen::BitmapConstRef<msdfgen::byte, 3> atlasBitmap =
            (msdfgen::BitmapConstRef<msdfgen::byte, 3>) generator.atlasStorage();

    std::vector<uint8_t> rgbaData(atlas_width_ * atlas_height_ * 4);
    for (uint32_t y = 0; y < atlas_height_; ++y) {
        for (uint32_t x = 0; x < atlas_width_; ++x) {
            // atlasBitmap 使用 Y-up 坐标，需要翻转为 Y-down (屏幕空间)
            int srcY = (int) (atlas_height_ - 1 - y);
            const msdfgen::byte* pixel = atlasBitmap((int) x, srcY);
            uint32_t idx = (y * atlas_width_ + x) * 4;
            rgbaData[idx + 0] = pixel[0];
            rgbaData[idx + 1] = pixel[1];
            rgbaData[idx + 2] = pixel[2];
            rgbaData[idx + 3] = 0xFF;  // Alpha 全部 255
        }
    }

    // --- 8. 提取字形信息 ---
    for (const auto& glyph : glyphs) {
        if (glyph.isWhitespace())
            continue;

        GlyphInfo info;
        info.unicode = glyph.getCodepoint();
        info.advanceX = static_cast<float>(glyph.getAdvance() * base_font_size_);

        // 平面边界（字形相对基线的位置）
        double pl, pb, pr, pt;
        glyph.getQuadPlaneBounds(pl, pb, pr, pt);
        // planeBounds: Y-up 坐标，转换为 Y-down
        // pl=left, pb=bottom(up), pr=right, pt=top(up)
        // 在 Y-down 屏幕空间：top 是向下的
        info.planeLeft = static_cast<float>(pl * base_font_size_);
        info.planeTop = static_cast<float>(pt * base_font_size_);  // 基线到字形顶部的偏移（Y-up 正值）
        info.width = static_cast<float>((pr - pl) * base_font_size_);
        info.height = static_cast<float>((pt - pb) * base_font_size_);

        // Atlas 中的 UV 边界
        double al, ab, ar, at;
        glyph.getQuadAtlasBounds(al, ab, ar, at);
        // atlasBounds 是 Y-up，需要翻转
        info.atlasU = (float) (al / (double) atlas_width_);
        info.atlasV = (float) (1.0 - at / (double) atlas_height_);   // 翻转 Y
        info.atlasU2 = (float) (ar / (double) atlas_width_);
        info.atlasV2 = (float) (1.0 - ab / (double) atlas_height_);  // 翻转 Y

        glyphs_[info.unicode] = info;
    }

    // --- 9. 上传 GPU 纹理 ---
    bool uploaded = uploadAtlas(rgbaData);

    // --- 10. 创建采样器 ---
    if (uploaded) {
        SamplerDesc samplerDesc;
        samplerDesc.minFilter = SamplerFilter::Linear;
        samplerDesc.magFilter = SamplerFilter::Linear;
        samplerDesc.addressU = SamplerAddressMode::ClampToEdge;
        samplerDesc.addressV = SamplerAddressMode::ClampToEdge;
        samplerDesc.debugName = "MSDF_Atlas_Sampler";
        auto sampler = device_->createSampler(samplerDesc);
        if (!sampler) {
            std::fprintf(stderr, "[FontAtlas] Failed to create atlas sampler: %s\n", sampler.error().message.c_str());
        } else {
            sampler_ = std::move(*sampler);
        }
    }

    // --- 11. 清理 ---
    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);

    return uploaded;
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
        std::fprintf(stderr, "[FontAtlas] Invalid atlas upload request\n");
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
        std::fprintf(stderr, "[FontAtlas] Failed to create atlas texture: %s\n", result.error().message.c_str());
        return false;
    }
    texture_ = std::move(*result);

    // 当前引擎的 TextureDesc 没有 initData 字段，纹理内容通过 RHIDevice 的统一上传入口写入。
    // 后端相关的 UploadContext 与布局转换细节留在 RHI 层，FontAtlas 不直接依赖具体后端。
    device_->uploadTextureData(texture_.get(), rgbaData.data(), atlas_width_, atlas_height_,
                               TextureFormat::RGBA8_UNorm);

    return true;
}

}  // namespace mulan::engine
