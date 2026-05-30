/**
 * @file GLPipelineState.h
 * @brief OpenGL 管线状态实现
 * @author terry
 * @date 2026-04-22
 *
 * OpenGL 没有 monolithic PSO（管线状态对象）。我们存储管线描述和编译后的
 * 着色器程序（link）。在 bind 时依次设置各项 GL 状态。
 *
 * 设计：
 * - 构造时存储 desc，link 着色器程序（VS + PS + GS if any）
 * - finalize() 时验证，绑定片段输出位置等（需要 render target 信息）
 * - bind 时设置 program，以及光栅化、深度、混合等全部状态
 */

#pragma once

#include "../PipelineState.h"
#include "GLCommon.h"

#include <string_view>

namespace mulan::engine {

class GLPipelineState final : public PipelineState {
public:
    /// 构造：存储描述，link 着色器程序
    explicit GLPipelineState(const GraphicsPipelineDesc& desc);

    ~GLPipelineState();

    // --- PipelineState 接口实现 ---

    const GraphicsPipelineDesc& desc() const override { return m_desc; }

    // --- OpenGL 特有接口 ---

    /// 获取链接的着色器程序
    GLuint program() const { return m_program; }

    /// 检查管线是否成功链接
    bool isValid() const { return m_program != 0; }

    /// 应用管线状态（光栅化、深度、混合等）
    /// 注意：program binding 由 CommandList 负责
    void applyRenderState() const;

    /// 返回对应 PrimitiveTopology 的 GL 图元枚举
    GLenum glTopology() const;

private:
    // 构建着色器程序（link VS + PS + GS）
    void linkProgram();

    // 应用各项状态
    void applyRasterizerState() const;
    void applyDepthStencilState() const;
    void applyBlendState() const;
    void applyTopology() const;

    // 成员
    GraphicsPipelineDesc m_desc;
    GLuint               m_program = 0;  // Linked shader program
};

} // namespace mulan::engine
