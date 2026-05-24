/**
 * @file SceneRenderer.h
 * @brief 场景渲染器 — 管理管线资源（Shader/PSO/UBO），录制 RHI 绘制命令
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../RHI/Device.h"
#include "../RHI/CommandList.h"
#include "../RHI/Shader.h"
#include "../RHI/PipelineState.h"
#include "../RHI/Buffer.h"
#include "../RHI/VertexLayout.h"
#include "../RHI/SwapChain.h"
#include "../RHI/RenderTarget.h"
#include "../Scene/Camera/Camera.h"
#include "RenderGeometry.h"
#include "LightEnvironment.h"
#include "Material/Material.h"
#include "Material/MaterialInstance.h"
#include "Material/MaterialCache.h"
#include "Pass/ForwardPass.h"
#include "Pass/RenderPass.h"
#include "Text/TextRenderer.h"
#include "Text/FontAtlas.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace MulanGeo::engine {

// ============================================================
// 渲染模式
// ============================================================

enum class RenderMode : uint8_t {
    Solid,       // 实体填充
    Wireframe,   // 线框
    SolidWire,   // 实体 + 线框叠加
};

// ============================================================
// 帧统计
// ============================================================

struct RenderStats {
    uint32_t drawCalls      = 0;
    uint32_t triangles      = 0;
    uint32_t lines          = 0;
    uint32_t items          = 0;
    uint32_t materialWrites = 0;  // MaterialUBO 实际写入次数
};

// ============================================================
// 材质状态 — 用于跳过冗余 MaterialUBO 写入
// ============================================================

struct MaterialState {
    uint16_t materialIndex = 0xFFFF;
    bool     selected      = false;

    bool operator!=(const MaterialState& o) const {
        return materialIndex != o.materialIndex || selected != o.selected;
    }
    bool operator==(const MaterialState& o) const { return !(*this != o); }
};

// ============================================================
// 场景渲染器
// ============================================================

class SceneRenderer {
public:
    explicit SceneRenderer(RHIDevice* device);

    friend class ForwardPass;

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void cleanup();

    void setRenderMode(RenderMode mode) { m_renderMode = mode; }
    RenderMode renderMode() const { return m_renderMode; }

    /// 添加 2D 屏幕文字（在 render() 末尾绘制）
    void addText(std::string_view text,
                 float x, float y,
                 float fontSize = 32.0f,
                 const float color[4] = nullptr);

    /// 设置字体（必须在首次 addText 前调用）
    void setFont(FontAtlas* font);

    void render(const RenderQueue& queue, const Camera& camera, CommandList* cmdList,
                const LightEnvironment& lightEnv);

    void setLightEnvironment(const LightEnvironment& env) { m_lightEnv = env; }
    LightEnvironment& lightEnvironment() { return m_lightEnv; }

    const RenderStats& stats() const { return m_stats; }

private:
    // --- GPU UBO 结构 (与 shader Common.hlsli 对齐) ---

    struct alignas(16) SceneUBO {
        float view[16];
        float projection[16];
        float viewProjection[16];
        float cameraPos[3];      float _pad0;
        float lightDir[3];       float _pad1;
        float lightColor[3];     float _pad2;
        float ambientColor[3];   float _pad3;
        float edgeColor[3];      float _pad4;
        float highlightColor[3]; float _pad5;
    };
    static_assert(sizeof(SceneUBO) == 288);

    struct alignas(16) ObjectUBO {
        float world[16];
        float normalMat[12];
        uint32_t pickId;
        uint32_t selected;
        float _pad[2];
    };
    static_assert(sizeof(ObjectUBO) == 128);

    // MaterialUBO = MaterialGPU (80 bytes), 直接 memcpy
    using MaterialUBO = MaterialGPU;

    // --- 内部方法 ---

    void loadShaders();
    void createPSOs(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void createUBOs();
    void updateSceneUBO(const Camera& camera);
    void drawItem(const RenderItem& item, CommandList* cmdList,
                  PipelineState* pso, bool isEdge);

    MaterialInstance* getOrCreateInstance(uint32_t materialId);

    // --- 设备 ---

    RHIDevice*   m_device;

    // --- Shader / PSO ---

    ResourcePtr<Shader>         m_solidVs;
    ResourcePtr<Shader>         m_solidFs;
    ResourcePtr<PipelineState>  m_solidPso;
    ResourcePtr<Shader>         m_edgeVs;
    ResourcePtr<Shader>         m_edgeFs;
    ResourcePtr<PipelineState>  m_edgePso;
    VertexLayout                m_vertexLayout;

    // --- UBO (单帧 ring buffer, fence 保证 GPU/CPU 同步) ---

    ResourcePtr<Buffer>         m_sceneBuffer;     // b0: sizeof(SceneUBO)
    ResourcePtr<Buffer>         m_objectBuffer;    // b1: kMaxDrawCalls × objectStride
    ResourcePtr<Buffer>         m_materialBuffer;  // b2: kMaxDrawCalls × materialStride

    static constexpr uint32_t   kMaxDrawCalls = 4096;
    uint32_t                    m_drawCallIndex = 0;
    uint32_t                    m_objectStride  = sizeof(ObjectUBO);
    uint32_t                    m_materialStride = sizeof(MaterialUBO);

    // --- 材质 ---

    std::unordered_map<uint32_t, std::unique_ptr<MaterialInstance>> m_instances;
    MaterialInstance*           m_defaultMaterial = nullptr;
    MaterialState               m_currentMatState;

    // --- Pass 管线 ---

    std::unique_ptr<ForwardPass>                m_forwardPass;
    std::vector<RenderPass*>                    m_passes;  // 有序 Pass 列表

    // --- 文字渲染 ---

    std::unique_ptr<TextRenderer>               m_textRenderer;
    FontAtlas*                                  m_font = nullptr;

    // --- 状态 ---

    RenderMode        m_renderMode = RenderMode::Solid;
    LightEnvironment  m_lightEnv;
    RenderStats       m_stats;
};

} // namespace MulanGeo::Engine
