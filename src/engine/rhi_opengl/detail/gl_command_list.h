/**
 * @file gl_command_list.h
 * @brief OpenGL 命令列表实现
 * @author terry
 * @date 2026-04-16
 *
 * OpenGL 是立即模式 (Immediate Mode) 渲染：
 * - 命令在录制时立即执行（与 Vulkan/D3D12 的延迟模式不同）
 * - 这里的 CommandList 主要做数据收集和状态应用
 * - begin()/end() 不做任何实质操作
 * - 所有 set* 方法在调用时直接执行 GL 命令
 *
 * 工作流程:
 *   frameCommandList = device->frameCommandList()
 *   frameCommandList->begin()
 *   frameCommandList->setPipelineState(...)
 *   frameCommandList->setVertexBuffer(...)
 *   frameCommandList->draw(...)
 *   frameCommandList->end()
 *   device->submitAndPresent(swapchain)  // 无op，命令已执行
 */

#pragma once

#include "gl_common.h"
#include "../../rhi/command_list.h"

#include <cstdint>

namespace mulan::engine {

using graphics::VertexFormat;

// 前向声明
class GLBuffer;
class GLPipelineState;
class GLTexture;
class GLSampler;

class GLCommandList final : public CommandList {
public:
    /// 构造函数
    GLCommandList();

    ~GLCommandList();

    // --- 生命周期 ---

    /// 开始录制命令（对 GL 无实际意义，但保持接口一致）
    void begin() override;

    /// 结束录制命令
    void end() override;

    // --- 管线状态 ---

    /// 设置当前管线状态
    void setPipelineState(PipelineState* pso) override;

    // --- 资源绑定 ---

    /// 绑定资源组（UBO / Texture）
    void bindGroup(BindGroup& group) override;
    void bindResources(const BindGroupDesc& desc) override;

    // --- 视口 / 裁剪 ---

    /// 设置视口
    void setViewport(const Viewport& vp) override;

    /// 设置裁剪矩形
    void setScissorRect(const ScissorRect& rect) override;

    // --- 缓冲区绑定 ---

    /// 绑定单个顶点缓冲区
    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) override;

    /// 绑定多个顶点缓冲区
    void setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;

    /// 绑定索引缓冲区
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0, IndexType type = IndexType::UInt32) override;

    // --- 绘制 ---

    /// 非索引绘制
    void draw(const DrawAttribs& attribs) override;

    /// 索引绘制
    void drawIndexed(const DrawIndexedAttribs& attribs) override;

    // --- 资源更新 ---

    /// 更新缓冲区数据
    void updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                      ResourceTransitionMode mode = ResourceTransitionMode::Transition) override;

    // --- 资源状态转换（GL 无需显式） ---

    /// GL 中无需显式资源状态转换（当前为无op）
    void transitionResource(Buffer* buffer, ResourceState newState) override;

    /// GL 中纹理状态转换（当前为无op）
    void transitionResource(Texture* texture, ResourceState newState) override;

    /// 复制纹理到缓冲区（用于 GPU→CPU 数据回读）
    bool copyTextureToBuffer(Texture* src, Buffer* dst) override;

    // --- 清除 ---

    /// 清除颜色缓冲区
    void clearColor(float r, float g, float b, float a) override;

    /// 清除深度缓冲区
    void clearDepth(float depth) override;

    /// 清除模板缓冲区
    void clearStencil(uint8_t stencil) override;

    // --- RenderPass ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;

    // --- OpenGL 特有接口 ---

    /// 获取当前绑定的管线状态
    PipelineState* currentPipelineState() const { return current_pipeline_; }

    /// 获取当前绑定的顶点缓冲区
    Buffer* currentVertexBuffer(uint32_t slot) const;

    /// 获取当前绑定的索引缓冲区
    Buffer* currentIndexBuffer() const { return index_buffer_; }

    /// 检查是否处于有效状态
    bool isValid() const { return true; }

private:
    // --- 内部方法 ---

    /// 应用当前管线状态到 GL
    void applyPipelineState();

    /// 按 VertexLayout 通过 DSA 重新设置顶点属性
    void setupVertexAttributes();

    /// 转换 IndexType 到 GL 格式
    static GLenum indexTypeToGLFormat(IndexType type);

    /// 将 VertexFormat 映射到 GL 类型信息
    struct GLAttribType {
        GLenum type;
        GLint components;
        GLboolean normalized;
        bool isInteger;
    };
    static GLAttribType vertexFormatToGL(VertexFormat fmt);
    void bindResources(const BindGroup& group);
    void bindEntry(const BindGroupEntry& entry);

    // --- 成员变量 ---

    PipelineState* current_pipeline_ = nullptr;

    // 顶点缓冲区绑定（可能有多个）
    static constexpr uint32_t MAX_VERTEX_BUFFERS = 16;
    Buffer* vertex_buffers_[MAX_VERTEX_BUFFERS] = {};
    uint32_t vertex_buffer_offsets_[MAX_VERTEX_BUFFERS] = {};
    uint32_t vertex_buffer_count_ = 0;

    // 索引缓冲区绑定
    Buffer* index_buffer_ = nullptr;
    uint32_t index_buffer_Offset = 0;
    IndexType index_type_ = IndexType::UInt32;

    // 视口和裁剪
    Viewport viewport_ = {};
    ScissorRect scissor_rect_ = {};
    bool viewport_Dirty = false;
    bool scissor_dirty_ = false;
    int32_t framebuffer_height_ = 0;
    GLuint previous_framebuffer_ = 0;
    GLuint active_framebuffer_ = 0;
    bool render_pass_active_ = false;
    GLTexture* resolve_source_ = nullptr;
    GLTexture* resolve_target_ = nullptr;

    // 记录是否已应用状态（避免冗余的 GL 调用）
    bool pipeline_state_applied_ = false;

    // VAO — OpenGL Core Profile 必须绑定 VAO 才能 draw
    GLuint vao_ = 0;
    bool vertex_layout_dirty_ = true;  // VBO/PSO 变化时重新设置属性指针
};

}  // namespace mulan::engine
