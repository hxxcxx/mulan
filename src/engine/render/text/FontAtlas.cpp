/**
 * @file FontAtlas.cpp
 * @brief MSDF 字体图集实现 — 使用 msdf-atlas-gen 生成图集
 * @author hxxcxx
 * @date 2026-04-27
 */

#include "FontAtlas.h"

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

FontAtlas::FontAtlas(RHIDevice* device)
    : m_device(device) {}

FontAtlas::~FontAtlas() = default;

// ============================================================
// 加载字体并生成 MSDF 图集
// ============================================================

bool FontAtlas::load(const char* fontPath, float fontSize,
                     uint32_t atlasWidth, uint32_t atlasHeight) {
    m_baseFontSize = fontSize;
    m_atlasWidth   = atlasWidth;
    m_atlasHeight  = atlasHeight;

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
    for (auto c : {0x3001u, 0x3002u, 0xFF0Cu, 0xFF0Eu, 0xFF08u, 0xFF09u,
                   0x2018u, 0x2019u, 0x201Cu, 0x201Du})
        charset.add(c);

    int loaded = fontGeometry.loadCharset(font, 1.0, charset);
    std::fprintf(stderr, "[FontAtlas] Loaded %d glyphs from %s\n", loaded, fontPath);

    // --- 4. MSDF 边缘着色 ---
    const double maxCornerAngle = 3.0;
    for (auto& glyph : glyphs)
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);

    // --- 5. 紧凑打包 ---
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensions((int)atlasWidth, (int)atlasHeight);
    packer.setScale((double)fontSize);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(glyphs.data(), (int)glyphs.size());

    // 获取实际尺寸和像素范围
    int packW = 0, packH = 0;
    packer.getDimensions(packW, packH);
    m_atlasWidth  = (uint32_t)std::max(packW, 1);
    m_atlasHeight = (uint32_t)std::max(packH, 1);
    m_pxRange     = (float)(packer.getPixelRange().upper - packer.getPixelRange().lower);

    // --- 6. 生成 MSDF 位图 ---
    using AtlasGenerator = msdf_atlas::ImmediateAtlasGenerator<
        float, 3,
        msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<msdfgen::byte, 3>
    >;

    AtlasGenerator generator((int)m_atlasWidth, (int)m_atlasHeight);
    msdf_atlas::GeneratorAttributes attr;
    generator.setAttributes(attr);
    generator.setThreadCount(4);
    generator.generate(glyphs.data(), (int)glyphs.size());

    // --- 7. 获取位图数据并转换为 RGBA8 ---
    msdfgen::BitmapConstRef<msdfgen::byte, 3> atlasBitmap =
        (msdfgen::BitmapConstRef<msdfgen::byte, 3>) generator.atlasStorage();

    std::vector<uint8_t> rgbaData(m_atlasWidth * m_atlasHeight * 4);
    for (uint32_t y = 0; y < m_atlasHeight; ++y) {
        for (uint32_t x = 0; x < m_atlasWidth; ++x) {
            // atlasBitmap 使用 Y-up 坐标，需要翻转为 Y-down (屏幕空间)
            int srcY = (int)(m_atlasHeight - 1 - y);
            const msdfgen::byte* pixel = atlasBitmap((int)x, srcY);
            uint32_t idx = (y * m_atlasWidth + x) * 4;
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
        info.unicode   = glyph.getCodepoint();
        info.advanceX  = (float)glyph.getAdvance();

        // 平面边界（字形相对基线的位置）
        double pl, pb, pr, pt;
        glyph.getQuadPlaneBounds(pl, pb, pr, pt);
        // planeBounds: Y-up 坐标，转换为 Y-down
        // pl=left, pb=bottom(up), pr=right, pt=top(up)
        // 在 Y-down 屏幕空间：top 是向下的
        info.planeLeft = (float)pl;
        info.planeTop  = (float)pt;   // 基线到字形顶部的偏移（Y-up 正值）
        info.width     = (float)(pr - pl);
        info.height    = (float)(pt - pb);

        // Atlas 中的 UV 边界
        double al, ab, ar, at;
        glyph.getQuadAtlasBounds(al, ab, ar, at);
        // atlasBounds 是 Y-up，需要翻转
        info.atlasU  = (float)(al / (double)m_atlasWidth);
        info.atlasV  = (float)(1.0 - at / (double)m_atlasHeight);  // 翻转 Y
        info.atlasU2 = (float)(ar / (double)m_atlasWidth);
        info.atlasV2 = (float)(1.0 - ab / (double)m_atlasHeight);  // 翻转 Y

        m_glyphs[info.unicode] = info;
    }

    // --- 9. 上传 GPU 纹理 ---
    bool uploaded = uploadAtlas(rgbaData);

    // --- 10. 创建采样器 ---
    if (uploaded) {
        SamplerDesc samplerDesc;
        samplerDesc.minFilter   = SamplerFilter::Linear;
        samplerDesc.magFilter   = SamplerFilter::Linear;
        samplerDesc.addressU    = SamplerAddressMode::ClampToEdge;
        samplerDesc.addressV    = SamplerAddressMode::ClampToEdge;
        samplerDesc.debugName   = "MSDF_Atlas_Sampler";
        m_sampler = m_device->createSampler(samplerDesc);
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
    auto it = m_glyphs.find(unicode);
    return it != m_glyphs.end() ? &it->second : nullptr;
}

// ============================================================
// 上传图集纹理到 GPU
// ============================================================

bool FontAtlas::uploadAtlas(const std::vector<uint8_t>& rgbaData) {
    TextureDesc texDesc;
    texDesc.name      = "MSDF_Atlas";
    texDesc.format    = TextureFormat::RGBA8_UNorm;
    texDesc.dimension = TextureDimension::Texture2D;
    texDesc.usage     = TextureUsageFlags::ShaderResource;
    texDesc.width     = m_atlasWidth;
    texDesc.height    = m_atlasHeight;
    texDesc.depth     = 1;
    texDesc.mipLevels = 1;
    texDesc.arraySize = 1;

    m_texture = m_device->createTexture(texDesc);
    if (!m_texture) {
        std::fprintf(stderr, "[FontAtlas] Failed to create atlas texture\n");
        return false;
    }

    // TODO: 实际的纹理上传需要通过各后端的 UploadContext 完成
    // 当前引擎的 TextureDesc 没有 initData 字段，
    // 后续激活时需在此处调用后端相关的 upload 命令。
    // 当前状态：纹理已创建但内容为空，编译通过即可。
    (void)rgbaData;

    return true;
}

} // namespace mulan::engine
