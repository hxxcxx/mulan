/**
 * @file TextRenderer.h
 * @brief MSDF 文字渲染器 — 管理 PSO + 动态缓冲 + 绘制调用
 * @author hxxcxx
 * @date 2026-04-27
 */

#pragma once

#include "TextTypes.h"
#include "FontAtlas.h"

#include "../../rhi/Device.h"
#include "../../rhi/PipelineState.h"
#include "../../rhi/Buffer.h"
#include "../../rhi/Shader.h"
#include "../../rhi/Sampler.h"

#include <memory>
#include <vector>

namespace mulan::engine {

// ============================================================
// MSDF 文字渲染器
// ============================================================

class TextRenderer {
public:
    explicit TextRenderer(RHIDevice* device);
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// 初始化：编译 shader、创建 PSO
    /// @param colorFmt 渲染目标颜色格式
    /// @param depthFmt 深度缓冲格式
    bool init(TextureFormat colorFmt, TextureFormat depthFmt);

    /// 释放资源
    void cleanup();

    /// 设置当前字体（必须在 render() 之前调用）
    void setFont(FontAtlas* font);

    /// 添加一段文字（屏幕空间，左上角为原点）
    /// @param text     UTF-8 文本
    /// @param x, y     起始位置（像素）
    /// @param fontSize 字号（像素）
    /// @param color    RGBA 颜色（线性空间，nullptr = 白色不透明）
    void addText(std::string_view text,
                 float x, float y,
                 float fontSize = 32.0f,
                 const float color[4] = nullptr);

    /// 渲染所有待绘制的文字，完成后清空队列
    /// @param cmd     命令列表
    /// @param width   视口宽度
    /// @param height  视口高度
    void render(CommandList* cmd, uint32_t width, uint32_t height);

    /// 清空待绘制队列
    void clear();

    /// 是否已初始化
    bool isInitialized() const { return m_textPso != nullptr; }

private:
    void loadShaders();
    void createPSO(TextureFormat colorFmt, TextureFormat depthFmt);

    RHIDevice*                    m_device;
    FontAtlas*                    m_font = nullptr;

    // Shader / PSO
    ResourcePtr<Shader>           m_textVs;
    ResourcePtr<Shader>           m_textFs;
    ResourcePtr<PipelineState>    m_textPso;

    // 动态顶点/索引缓冲
    ResourcePtr<Buffer>           m_vertexBuffer;
    ResourcePtr<Buffer>           m_indexBuffer;
    uint32_t                      m_vertexCapacity = 0;
    uint32_t                      m_indexCapacity  = 0;

    // UBO: 文字参数
    ResourcePtr<Buffer>           m_textUbo;

    /// 文字 UBO 布局（与 shader 对齐）
    struct alignas(16) TextUBO {
        float orthoProjection[16];   ///< 正交投影矩阵
        float bgColor[4];            ///< 背景色（默认透明）
        float pxRange;               ///< MSDF 像素范围
        float _pad[3];
    };

    // 每帧待绘制队列
    std::vector<TextDrawItem>     m_drawItems;
    // 排版后的顶点/索引缓存
    std::vector<TextVertex>       m_vertices;
    std::vector<uint32_t>         m_indices;
};

} // namespace mulan::engine
