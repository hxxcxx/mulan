/**
    * @file text_stage.h
    * @brief 文字渲染阶段
    * @author hxxcxx
    * @date 2024-07-06
 */

#pragma once

#include "font_manager.h"
#include "text_types.h"
#include "../forward/render_stage.h"
#include "../../rhi/buffer.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/shader.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mulan::engine {

class TextStage final : public RenderStage {
public:
    explicit TextStage(RHIDevice& device);
    ~TextStage() override;

    TextStage(const TextStage&) = delete;
    TextStage& operator=(const TextStage&) = delete;

    std::string_view name() const override { return "Text"; }

    core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) override;
    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void beginFrame(uint32_t width, uint32_t height);
    void clear();
    void addText(const TextDrawDesc& desc);
    void addTextList(const TextDrawList& list);

    bool isInitialized() const { return initialized_; }
    bool hasFont() const;
    bool hasFont(std::string_view fontKey) const;
    TextMetrics measureText(std::string_view fontKey, std::string_view text, float sizePx) const;

private:
    struct TextBatch {
        std::string font;
        FontAtlas* atlas = nullptr;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    struct alignas(16) TextParamsGPU {
        float orthoProjection[16];
        float bgColor[4];
        float pxRange = 4.0f;
        float atlasSize[2]{};
        float _pad{};
    };

    static_assert(sizeof(TextParamsGPU) == 96);

    bool loadShaders();
    bool createPipeline(const RenderTargetInfo& target);
    bool createBuffers(uint32_t vertexCapacity, uint32_t indexCapacity);
    bool ensureCapacity(uint32_t vertexCount, uint32_t indexCount);
    bool loadDefaultFont();
    BindGroup* bindGroupForFont(std::string_view fontKey, FontAtlas& font);
    FontAtlas* resolveFont(std::string_view fontKey) const;
    void buildGeometry();
    void updateParams(const FontAtlas& font);

    RHIDevice* device_ = nullptr;

    std::unique_ptr<Shader> vs_;
    std::unique_ptr<Shader> fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unordered_map<std::string, std::unique_ptr<BindGroup>> font_bind_groups_;

    std::unique_ptr<Buffer> params_ubo_;
    std::unique_ptr<Buffer> vertex_buffer_;
    std::unique_ptr<Buffer> index_buffer_;

    std::unique_ptr<FontManager> font_manager_;
    FontAtlas* default_font_ = nullptr;

    std::vector<TextDrawDesc> items_;
    std::vector<TextVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<TextBatch> batches_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t vertex_capacity_ = 0;
    uint32_t index_capacity_ = 0;
    bool initialized_ = false;
};

}  // namespace mulan::engine
