#include "detail/gl_pipeline_state.h"
#include "detail/gl_shader.h"
#include "../rhi/render_state.h"
#include "../rhi/render_types.h"

#include <algorithm>
#include <string>

namespace mulan::engine {

// ============================================================
// OpenGL 状态转换工具
// ============================================================

namespace {

/// 获取 OpenGL CullFace 模式
GLenum getGLCullFace(CullMode mode) {
    switch (mode) {
    case CullMode::None: return 0;  // 禁用 culling
    case CullMode::Front: return GL_FRONT;
    case CullMode::Back: return GL_BACK;
    default: return GL_BACK;
    }
}

/// 获取 OpenGL FrontFace 方向
GLenum getGLFrontFace(FrontFace face) {
    switch (face) {
    case FrontFace::CounterClockwise: return GL_CCW;
    case FrontFace::Clockwise: return GL_CW;
    default: return GL_CCW;
    }
}

/// 获取 OpenGL PolygonMode
GLenum getGLPolygonMode(FillMode mode) {
    switch (mode) {
    case FillMode::Solid: return GL_FILL;
    case FillMode::Wireframe: return GL_LINE;
    default: return GL_FILL;
    }
}

/// 获取 OpenGL 比较函数
GLenum getGLCompareFunc(CompareFunc func) {
    switch (func) {
    case CompareFunc::Never: return GL_NEVER;
    case CompareFunc::Less: return GL_LESS;
    case CompareFunc::Equal: return GL_EQUAL;
    case CompareFunc::LessEqual: return GL_LEQUAL;
    case CompareFunc::Greater: return GL_GREATER;
    case CompareFunc::NotEqual: return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always: return GL_ALWAYS;
    default: return GL_ALWAYS;
    }
}

/// 获取 OpenGL 模板操作
GLenum getGLStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep: return GL_KEEP;
    case StencilOp::Zero: return GL_ZERO;
    case StencilOp::Replace: return GL_REPLACE;
    case StencilOp::IncrementClamp: return GL_INCR;
    case StencilOp::DecrementClamp: return GL_DECR;
    case StencilOp::Invert: return GL_INVERT;
    case StencilOp::IncrementWrap: return GL_INCR_WRAP;
    case StencilOp::DecrementWrap: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

/// 获取 OpenGL BlendFactor
GLenum getGLBlendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcColor: return GL_SRC_COLOR;
    case BlendFactor::InvSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
    case BlendFactor::InvDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor: return GL_DST_COLOR;
    case BlendFactor::InvDstColor: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
    }
}

/// 获取 OpenGL BlendEquation
GLenum getGLBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return GL_FUNC_ADD;
    case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
    case BlendOp::RevSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min: return GL_MIN;
    case BlendOp::Max: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

/// 获取 OpenGL 图元类型
GLenum getGLPrimitiveType(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList: return GL_POINTS;
    case PrimitiveTopology::LineList: return GL_LINES;
    case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
    case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
    // Adjacency primitive types are available on desktop OpenGL.
    case PrimitiveTopology::LineListAdj: return GL_LINES_ADJACENCY;
    case PrimitiveTopology::LineStripAdj: return GL_LINE_STRIP_ADJACENCY;
    case PrimitiveTopology::TriangleListAdj: return GL_TRIANGLES_ADJACENCY;
    case PrimitiveTopology::TriangleStripAdj: return GL_TRIANGLE_STRIP_ADJACENCY;
    default: return GL_TRIANGLES;
    }
}

}  // anonymous namespace

// ============================================================
// GLPipelineState 实现
// ============================================================

GLPipelineState::GLPipelineState(const GraphicsPipelineDesc& desc) : desc_(desc) {
    linkProgram();
    desc_.discardShaderReferences();
}

GLPipelineState::~GLPipelineState() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void GLPipelineState::linkProgram() {
    if (!desc_.vs || !desc_.ps) {
        LOG_ERROR("[OpenGL] Pipeline creation failed: missing vertex or pixel shader, name={}", desc_.name);
        return;
    }

    // 获取编译后的着色器对象
    GLShader* vs = static_cast<GLShader*>(desc_.vs);
    GLShader* ps = static_cast<GLShader*>(desc_.ps);

    if (!vs->isValid() || !ps->isValid()) {
        LOG_ERROR("[OpenGL] Pipeline creation failed: invalid shader, name={}", desc_.name);
        return;
    }

    // 创建程序对象
    program_ = glCreateProgram();
    if (program_ == 0) {
        LOG_ERROR("[OpenGL] Pipeline creation failed: glCreateProgram returned 0");
        return;
    }

    // 附加着色器
    glAttachShader(program_, vs->handle());
    glAttachShader(program_, ps->handle());

    // 如果提供了 geometry shader，也附加
    if (desc_.gs) {
        GLShader* gs = static_cast<GLShader*>(desc_.gs);
        if (gs->isValid()) {
            glAttachShader(program_, gs->handle());
        }
    }

    // 链接
    glLinkProgram(program_);

    // 检查链接状态
    GLint linkStatus = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        GLint logLen = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLen);

        if (logLen > 0) {
            char* logBuf = new char[logLen];
            glGetProgramInfoLog(program_, logLen, nullptr, logBuf);
            LOG_ERROR("[OpenGL] Program link failed: name={}, diagnostic={}", desc_.name, logBuf);
            delete[] logBuf;
        } else {
            LOG_ERROR("[OpenGL] Program link failed: name={}, no diagnostic", desc_.name);
        }

        glDeleteProgram(program_);
        program_ = 0;
        return;
    }

    // 分离着色器（可选，程序已链接后可安全分离）
    glDetachShader(program_, vs->handle());
    glDetachShader(program_, ps->handle());
    if (desc_.gs) {
        GLShader* gs = static_cast<GLShader*>(desc_.gs);
        if (gs->isValid()) {
            glDetachShader(program_, gs->handle());
        }
    }

    LOG_DEBUG("[OpenGL] Program linked: name={}, handle={}", desc_.name, program_);
}

void GLPipelineState::applyRenderState() const {
    if (program_ == 0)
        return;

    applyRasterizerState();
    applyDepthStencilState();
    applyBlendState();
    applyTopology();
}

void GLPipelineState::applyRasterizerState() const {
    const auto& desc = desc_;

    // 剔除模式
    GLenum cullFace = getGLCullFace(desc.cullMode);
    if (cullFace != 0) {
        glEnable(GL_CULL_FACE);
        glCullFace(cullFace);
    } else {
        glDisable(GL_CULL_FACE);
    }

    // 正面方向
    glFrontFace(getGLFrontFace(desc.frontFace));

    // Desktop OpenGL polygon mode.
    GLenum polyMode = getGLPolygonMode(desc.fillMode);
    glPolygonMode(GL_FRONT_AND_BACK, polyMode);
}

void GLPipelineState::applyDepthStencilState() const {
    const auto& ds = desc_.depthStencil;

    // 深度测试
    if (ds.depthEnable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(getGLCompareFunc(ds.depthFunc));
        glDepthMask(ds.depthWrite ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    // 深度偏移（用于边线渲染防 z-fighting）
    if (ds.depthBias != 0.0f || ds.slopeScaledDepthBias != 0.0f) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(ds.slopeScaledDepthBias, ds.depthBias);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // 模板测试
    if (ds.stencilEnable) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(ds.stencilWriteMask);

        // 正面
        glStencilOpSeparate(GL_FRONT, getGLStencilOp(ds.frontFace.failOp), getGLStencilOp(ds.frontFace.depthFailOp),
                            getGLStencilOp(ds.frontFace.passOp));
        glStencilFuncSeparate(GL_FRONT, getGLCompareFunc(ds.frontFace.func),
                              0,  // ref（通常由 CommandList 在绘制时设置）
                              ds.stencilReadMask);

        // 背面
        glStencilOpSeparate(GL_BACK, getGLStencilOp(ds.backFace.failOp), getGLStencilOp(ds.backFace.depthFailOp),
                            getGLStencilOp(ds.backFace.passOp));
        glStencilFuncSeparate(GL_BACK, getGLCompareFunc(ds.backFace.func), 0, ds.stencilReadMask);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}

void GLPipelineState::applyBlendState() const {
    const auto& blend = desc_.blend;
    const uint32_t target_count = blend.independentBlend ? std::max<uint32_t>(1, desc_.colorTargetCount) : 1;

    if (!blend.independentBlend) {
        for (uint32_t i = 0; i < 8; ++i)
            glDisablei(GL_BLEND, i);
    }

    for (uint32_t i = 0; i < 8; ++i) {
        const auto& target = blend.renderTargets[i];
        const bool enabled = i < target_count && target.blendEnable;
        if (blend.independentBlend)
            (enabled ? glEnablei : glDisablei)(GL_BLEND, i);
        else if (i == 0) {
            if (enabled)
                glEnable(GL_BLEND);
            else
                glDisable(GL_BLEND);
        }

        if (!enabled) {
            const uint8_t mask = target.writeMask;
            if (blend.independentBlend)
                glColorMaski(i, (mask & 0x01) != 0, (mask & 0x02) != 0, (mask & 0x04) != 0, (mask & 0x08) != 0);
            else if (i == 0)
                glColorMask((mask & 0x01) != 0, (mask & 0x02) != 0, (mask & 0x04) != 0, (mask & 0x08) != 0);
            continue;
        }

        const GLenum src_color = getGLBlendFactor(target.srcBlend);
        const GLenum dst_color = getGLBlendFactor(target.dstBlend);
        const GLenum src_alpha = getGLBlendFactor(target.srcBlendAlpha);
        const GLenum dst_alpha = getGLBlendFactor(target.dstBlendAlpha);
        if (blend.independentBlend)
            glBlendFuncSeparatei(i, src_color, dst_color, src_alpha, dst_alpha);
        else
            glBlendFuncSeparate(src_color, dst_color, src_alpha, dst_alpha);

        const GLenum color_op = getGLBlendOp(target.blendOp);
        const GLenum alpha_op = getGLBlendOp(target.blendOpAlpha);
        if (blend.independentBlend)
            glBlendEquationSeparatei(i, color_op, alpha_op);
        else
            glBlendEquationSeparate(color_op, alpha_op);

        const uint8_t mask = target.writeMask;
        if (blend.independentBlend)
            glColorMaski(i, (mask & 0x01) != 0, (mask & 0x02) != 0, (mask & 0x04) != 0, (mask & 0x08) != 0);
        else
            glColorMask((mask & 0x01) != 0, (mask & 0x02) != 0, (mask & 0x04) != 0, (mask & 0x08) != 0);
    }

    if (!blend.independentBlend && !blend.renderTargets[0].blendEnable)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (blend.alphaToCoverage)
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

void GLPipelineState::applyTopology() const {
    // 图元类型在 glDrawXXX 时通过 glTopology() 传入，此处为 noop
}

GLenum GLPipelineState::glTopology() const {
    return getGLPrimitiveType(desc_.topology);
}

}  // namespace mulan::engine
