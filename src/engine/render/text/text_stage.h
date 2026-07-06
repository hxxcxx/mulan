/**
    * @file text_stage.h
    * @brief 文字渲染阶段
    * @author hxxcxx
    * @date 2024-07-06
 */

#pragma once

#include "font_atlas.h"
#include "text_types.h"
#include "../forward/render_stage.h"
#include "../../rhi/buffer.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/shader.h"

#include <memory>
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

    bool isInitialized() const { return initialized_; }
    bool hasFont() const { return default_font_ && default_font_->isLoaded(); }

private:
    struct alignas(16) TextParamsGPU {
        float orthoProjection[16];
        float bgColor[4];
        float pxRange = 4.0f;
        float _pad[3]{};
    };

    static_assert(sizeof(TextParamsGPU) == 96);

    bool loadShaders();
    bool createPipeline(const RenderTargetInfo& target);
    bool createBuffers(uint32_t vertexCapacity, uint32_t indexCapacity);
    bool ensureCapacity(uint32_t vertexCount, uint32_t indexCount);
    bool loadDefaultFont();
    bool createBindGroup();
    void buildGeometry();
    void updateParams();

    RHIDevice* device_ = nullptr;

    std::unique_ptr<Shader> vs_;
    std::unique_ptr<Shader> fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unique_ptr<BindGroup> bind_group_;

    std::unique_ptr<Buffer> params_ubo_;
    std::unique_ptr<Buffer> vertex_buffer_;
    std::unique_ptr<Buffer> index_buffer_;

    std::unique_ptr<FontAtlas> default_font_;

    std::vector<TextDrawDesc> items_;
    std::vector<TextVertex> vertices_;
    std::vector<uint32_t> indices_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t vertex_capacity_ = 0;
    uint32_t index_capacity_ = 0;
    bool initialized_ = false;
};

}  // namespace mulan::engine
