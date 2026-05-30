/**
 * @file FontAtlas.h
 * @brief MSDF 字体图集 — 字形查找 + GPU 纹理
 * @author hxxcxx
 * @date 2026-04-27
 */

#pragma once

#include "TextTypes.h"
#include "../../rhi/Device.h"
#include "../../rhi/Texture.h"
#include "../../rhi/Sampler.h"

#include <unordered_map>
#include <vector>

namespace mulan::engine {

// ============================================================
// 字体图集 — MSDF 纹理 + 字形查找表
// ============================================================

class FontAtlas {
public:
    explicit FontAtlas(RHIDevice* device);
    ~FontAtlas();

    // 不可拷贝
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    /// 加载字体文件并生成 MSDF 图集
    /// @param fontPath     TTF/OTF 字体文件路径
    /// @param fontSize     基准字号（像素，建议 32~64）
    /// @param atlasWidth   图集宽度（默认 1024）
    /// @param atlasHeight  图集高度（默认 1024）
    /// @return true 成功
    bool load(const char* fontPath,
              float fontSize = 48.0f,
              uint32_t atlasWidth = 1024,
              uint32_t atlasHeight = 1024);

    /// 查找字形信息，未找到返回 nullptr
    const GlyphInfo* getGlyph(uint32_t unicode) const;

    /// 获取 GPU 图集纹理（可能为 nullptr，load 之前）
    Texture* atlasTexture() const { return m_texture.get(); }

    /// 获取采样器
    Sampler* atlasSampler() const { return m_sampler.get(); }

    /// 基准字号（用于排版缩放计算）
    float baseFontSize() const { return m_baseFontSize; }

    /// MSDF 像素范围（传给 shader 的 pxRange）
    float pxRange() const { return m_pxRange; }

    /// 图集尺寸
    uint32_t atlasWidth() const { return m_atlasWidth; }
    uint32_t atlasHeight() const { return m_atlasHeight; }

    /// 是否已加载
    bool isLoaded() const { return m_texture != nullptr; }

private:
    /// 将 msdf-atlas-gen 生成的像素数据上传到 GPU
    bool uploadAtlas(const std::vector<uint8_t>& rgbaData);

    RHIDevice*                             m_device;
    ResourcePtr<Texture>                   m_texture;
    ResourcePtr<Sampler>                   m_sampler;
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;

    float     m_baseFontSize = 48.0f;
    float     m_pxRange      = 4.0f;
    uint32_t  m_atlasWidth   = 0;
    uint32_t  m_atlasHeight  = 0;
};

} // namespace mulan::engine
