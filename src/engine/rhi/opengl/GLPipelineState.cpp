/**
 * @file GLPipelineState.cpp
 * @brief OpenGL 管线状态实现
 * @author terry
 * @date 2026-04-22
 */

#include "GLPipelineState.h"
#include "GLShader.h"
#include "../RenderState.h"
#include "../VertexLayout.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

// ============================================================
// OpenGL 状态转换工具
// ============================================================

namespace {

/// 获取 OpenGL CullFace 模式
GLenum getGLCullFace(CullMode mode) {
    switch (mode) {
    case CullMode::None:   return 0;          // 禁用 culling
    case CullMode::Front:  return GL_FRONT;
    case CullMode::Back:   return GL_BACK;
    default:               return GL_BACK;
    }
}

/// 获取 OpenGL FrontFace 方向
GLenum getGLFrontFace(FrontFace face) {
    switch (face) {
    case FrontFace::CounterClockwise: return GL_CCW;
    case FrontFace::Clockwise:        return GL_CW;
    default:                          return GL_CCW;
    }
}

/// 获取 OpenGL PolygonMode
GLenum getGLPolygonMode(FillMode mode) {
#ifndef __EMSCRIPTEN__
    switch (mode) {
    case FillMode::Solid:      return GL_FILL;
    case FillMode::Wireframe:  return GL_LINE;
    default:                   return GL_FILL;
    }
#else
    (void)mode;
    return 0; // WebGL ES3 不支持 glPolygonMode
#endif
}

/// 获取 OpenGL 比较函数
GLenum getGLCompareFunc(CompareFunc func) {
    switch (func) {
    case CompareFunc::Never:        return GL_NEVER;
    case CompareFunc::Less:         return GL_LESS;
    case CompareFunc::Equal:        return GL_EQUAL;
    case CompareFunc::LessEqual:    return GL_LEQUAL;
    case CompareFunc::Greater:      return GL_GREATER;
    case CompareFunc::NotEqual:     return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always:       return GL_ALWAYS;
    default:                        return GL_ALWAYS;
    }
}

/// 获取 OpenGL 模板操作
GLenum getGLStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:             return GL_KEEP;
    case StencilOp::Zero:             return GL_ZERO;
    case StencilOp::Replace:          return GL_REPLACE;
    case StencilOp::IncrementClamp:   return GL_INCR;
    case StencilOp::DecrementClamp:   return GL_DECR;
    case StencilOp::Invert:           return GL_INVERT;
    case StencilOp::IncrementWrap:    return GL_INCR_WRAP;
    case StencilOp::DecrementWrap:    return GL_DECR_WRAP;
    default:                          return GL_KEEP;
    }
}

/// 获取 OpenGL BlendFactor
GLenum getGLBlendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:           return GL_ZERO;
    case BlendFactor::One:            return GL_ONE;
    case BlendFactor::SrcColor:       return GL_SRC_COLOR;
    case BlendFactor::InvSrcColor:    return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:       return GL_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:    return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:       return GL_DST_ALPHA;
    case BlendFactor::InvDstAlpha:    return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor:       return GL_DST_COLOR;
    case BlendFactor::InvDstColor:    return GL_ONE_MINUS_DST_COLOR;
    default:                          return GL_ONE;
    }
}

/// 获取 OpenGL BlendEquation
GLenum getGLBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:       return GL_FUNC_ADD;
    case BlendOp::Subtract:  return GL_FUNC_SUBTRACT;
    case BlendOp::RevSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min:       return GL_MIN;
    case BlendOp::Max:       return GL_MAX;
    default:                 return GL_FUNC_ADD;
    }
}

/// 获取 OpenGL 图元类型
GLenum getGLPrimitiveType(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList:      return GL_POINTS;
    case PrimitiveTopology::LineList:       return GL_LINES;
    case PrimitiveTopology::LineStrip:      return GL_LINE_STRIP;
    case PrimitiveTopology::TriangleList:   return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip:  return GL_TRIANGLE_STRIP;
    #ifndef __EMSCRIPTEN__
    // WebGL ES3 不支持邻接图元类型
    case PrimitiveTopology::LineListAdj:    return GL_LINES_ADJACENCY;
    case PrimitiveTopology::LineStripAdj:   return GL_LINE_STRIP_ADJACENCY;
    case PrimitiveTopology::TriangleListAdj: return GL_TRIANGLES_ADJACENCY;
    case PrimitiveTopology::TriangleStripAdj: return GL_TRIANGLE_STRIP_ADJACENCY;
#endif
    default:                                 return GL_TRIANGLES;
    }
}

} // anonymous namespace

// ============================================================
// GLPipelineState 实现
// ============================================================

GLPipelineState::GLPipelineState(const GraphicsPipelineDesc& desc)
    : m_desc(desc)
{
    linkProgram();
}

GLPipelineState::~GLPipelineState() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void GLPipelineState::linkProgram() {
    if (!m_desc.vs || !m_desc.ps) {
        std::fprintf(stderr,
            "[GLPipelineState] Missing vertex or pixel shader (name: %s)\n",
            std::string(m_desc.name).c_str());
        return;
    }

    // 获取编译后的着色器对象
    GLShader* vs = static_cast<GLShader*>(m_desc.vs);
    GLShader* ps = static_cast<GLShader*>(m_desc.ps);

    if (!vs->isValid() || !ps->isValid()) {
        std::fprintf(stderr,
            "[GLPipelineState] Invalid shader (name: %s)\n",
            std::string(m_desc.name).c_str());
        return;
    }

    // 创建程序对象
    m_program = glCreateProgram();
    if (m_program == 0) {
        std::fprintf(stderr, "[GLPipelineState] glCreateProgram failed\n");
        return;
    }

    // 附加着色器
    glAttachShader(m_program, vs->handle());
    glAttachShader(m_program, ps->handle());

    // 如果提供了 geometry shader，也附加
    if (m_desc.gs) {
        GLShader* gs = static_cast<GLShader*>(m_desc.gs);
        if (gs->isValid()) {
            glAttachShader(m_program, gs->handle());
        }
    }

    // 链接
    glLinkProgram(m_program);

    // 检查链接状态
    GLint linkStatus = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        GLint logLen = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);

        if (logLen > 0) {
            char* logBuf = new char[logLen];
            glGetProgramInfoLog(m_program, logLen, nullptr, logBuf);
            std::fprintf(stderr,
                "[GLPipelineState] Program link failed (name: %s):\n%s\n",
                std::string(m_desc.name).c_str(), logBuf);
            delete[] logBuf;
        } else {
            std::fprintf(stderr,
                "[GLPipelineState] Program link failed (name: %s)\n",
                std::string(m_desc.name).c_str());
        }

        glDeleteProgram(m_program);
        m_program = 0;
        return;
    }

    // 分离着色器（可选，程序已链接后可安全分离）
    glDetachShader(m_program, vs->handle());
    glDetachShader(m_program, ps->handle());
    if (m_desc.gs) {
        GLShader* gs = static_cast<GLShader*>(m_desc.gs);
        if (gs->isValid()) {
            glDetachShader(m_program, gs->handle());
        }
    }

    std::fprintf(stdout, "[GLPipelineState] Linked program (name: %s, handle: %u)\n",
        std::string(m_desc.name).c_str(), m_program);
}

void GLPipelineState::applyRenderState() const {
    if (m_program == 0) return;

    applyRasterizerState();
    applyDepthStencilState();
    applyBlendState();
    applyTopology();
}

void GLPipelineState::applyRasterizerState() const {
    const auto& desc = m_desc;

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

    // 多边形模式（WebGL ES3 不支持 glPolygonMode，仅桌面 OpenGL）
#ifndef __EMSCRIPTEN__
    GLenum polyMode = getGLPolygonMode(desc.fillMode);
    glPolygonMode(GL_FRONT_AND_BACK, polyMode);
#endif
}

void GLPipelineState::applyDepthStencilState() const {
    const auto& ds = m_desc.depthStencil;

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
        glStencilOpSeparate(GL_FRONT,
            getGLStencilOp(ds.frontFace.failOp),
            getGLStencilOp(ds.frontFace.depthFailOp),
            getGLStencilOp(ds.frontFace.passOp)
        );
        glStencilFuncSeparate(GL_FRONT,
            getGLCompareFunc(ds.frontFace.func),
            0,  // ref（通常由 CommandList 在绘制时设置）
            ds.stencilReadMask
        );

        // 背面
        glStencilOpSeparate(GL_BACK,
            getGLStencilOp(ds.backFace.failOp),
            getGLStencilOp(ds.backFace.depthFailOp),
            getGLStencilOp(ds.backFace.passOp)
        );
        glStencilFuncSeparate(GL_BACK,
            getGLCompareFunc(ds.backFace.func),
            0,
            ds.stencilReadMask
        );
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}

void GLPipelineState::applyBlendState() const {
    const auto& blend = m_desc.blend;

    if (blend.renderTargets[0].blendEnable) {
        glEnable(GL_BLEND);

        glBlendFuncSeparate(
            getGLBlendFactor(blend.renderTargets[0].srcBlend),
            getGLBlendFactor(blend.renderTargets[0].dstBlend),
            getGLBlendFactor(blend.renderTargets[0].srcBlendAlpha),
            getGLBlendFactor(blend.renderTargets[0].dstBlendAlpha)
        );

        glBlendEquationSeparate(
            getGLBlendOp(blend.renderTargets[0].blendOp),
            getGLBlendOp(blend.renderTargets[0].blendOpAlpha)
        );

        // 颜色写入掩码
        uint8_t mask = blend.renderTargets[0].writeMask;
        glColorMask(
            (mask & 0x01) != 0,  // R
            (mask & 0x02) != 0,  // G
            (mask & 0x04) != 0,  // B
            (mask & 0x08) != 0   // A
        );
    } else {
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    // Alpha-to-coverage (简化，仅当 MSAA 支持时)
    if (blend.alphaToCoverage) {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    } else {
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
}

void GLPipelineState::applyTopology() const {
    // 图元类型在 glDrawXXX 时通过 glTopology() 传入，此处为 noop
}

GLenum GLPipelineState::glTopology() const {
    return getGLPrimitiveType(m_desc.topology);
}

} // namespace mulan::Engine
