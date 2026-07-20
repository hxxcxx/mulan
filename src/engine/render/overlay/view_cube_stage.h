/**
 * @file view_cube_stage.h
 * @brief 导航立方体渲染阶段 — 在视口角落显示方向指示立方体
 * @author hxxcxx
 * @date 2026-05-26
 *
 * 职责：
 *  - 生成带顶点颜色的导航立方体几何体（6个面中心 + 12个共享边区 + 8个角区）
 *  - 从主相机提取旋转，以正交投影渲染
 *  - 使用视口裁剪将渲染限定在角落小区域
 *
 * 不负责：上层输入分发，只接收并渲染 ViewCubeInteractionState
 */

#pragma once

#include "view_cube_contract.h"
#include "../frame/render_frame.h"
#include "../frame/render_target_info.h"
#include "../gpu_scene_contract.h"
#include "../../rhi/buffer.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"

#include <mulan/core/result/error.h>

#include <cstdint>
#include <array>
#include <memory>
#include <vector>

namespace mulan::engine {

class CommandList;
class Sampler;
class TextDrawList;
class Texture;

/// 导航立方体渲染阶段
///
/// 不拥有 PSO / Shader —— 由 Renderer 注入几何绘制执行器的 PSO。
/// 只拥有 CPU 几何体、顶点颜色和小尺寸 UBO（与主场景布局不同）。
/// render() 从主相机提取旋转、设置角落视口、借用 PSO + BindGroupDesc 录制命令。
class ViewCubeStage final {
public:
    explicit ViewCubeStage(RHIDevice& device);
    ~ViewCubeStage();

    ViewCubeStage(const ViewCubeStage&) = delete;
    ViewCubeStage& operator=(const ViewCubeStage&) = delete;

    ResultVoid init(RHIDevice& device, const RenderTargetInfo& target);

    void shutdown(RHIDevice& device);
    void execute(RenderFrame& frame);

    void setPipelines(PipelineState* solidPso, PipelineState* edgePso);
    void setFallbackResources(Texture* defaultWhite, Sampler* defaultSampler);

    /// 设置 ViewCube 显示大小（像素）
    void setSize(uint32_t size);

    /// 设置边距（像素，距右下角）
    void setMargin(uint32_t margin);

    void setLayout(const ViewCubeLayout& layout);
    void setInteraction(const ViewCubeInteractionState& interaction);
    void setCorner(ViewCubeCorner corner);
    void collectLabels(TextDrawList& textDraws, const math::Mat4& mainViewMatrix, uint32_t vpWidth,
                       uint32_t vpHeight) const;

    bool isInitialized() const { return initialized_; }

private:
    // --- 内部类型 ---

    /// 立方体顶点格式：pos(3f) + normal(3f) + color(RGBA8) + pad = 32 bytes。
    struct CubeVertex {
        float pos[3];
        float normal[3];
        uint8_t color[4];
        uint32_t pad = 0;
    };

    /// 面信息
    struct FaceInfo {
        float normal[3];
        float up[3];
        float color[3];      // 面颜色（线性空间）
        float edgeColor[3];  // 边线颜色
    };

    // --- 内部方法 ---
    bool createGeometry();
    bool createFaceGeometry();
    bool createAxisGeometry();
    bool updateInteractionGeometry();

    void render(CommandList* cmd, const math::Mat4& mainViewMatrix, uint32_t vpWidth, uint32_t vpHeight);

    // --- 设备 ---
    RHIDevice* device_;
    PipelineState* solid_pso_ = nullptr;
    Texture* default_white_ = nullptr;
    Sampler* default_sampler_ = nullptr;

    // --- 几何缓冲 ---
    std::unique_ptr<Buffer> face_vb_;  // 面顶点
    std::unique_ptr<Buffer> face_ib_;  // 面索引
    uint32_t face_index_count_ = 0;
    std::vector<CubeVertex> face_vertices_;
    std::array<uint32_t, ViewCubeGeometry::kPartCount> part_vertex_offsets_{};
    std::array<uint32_t, ViewCubeGeometry::kPartCount> part_vertex_counts_{};
    std::unique_ptr<Buffer> axis_vb_;
    std::unique_ptr<Buffer> axis_ib_;
    static constexpr uint32_t kAxisCount = 3;
    static constexpr uint32_t kAxisSegments = 16;
    uint32_t axis_index_count_ = 0;

    static constexpr uint32_t kPartCount = ViewCubeGeometry::kPartCount;

    // --- per-frame BindGroup（按借用 PSO 的 layout 创建，缓存在 PSO 不变期间复用）---
    std::unique_ptr<BindGroup> face_bg_;  // solid PSO (10 binding)
    uint64_t face_bg_layout_hash_ = 0;

    // --- 所有部件共享一次材质绑定；实际颜色位于顶点中 ---
    MaterialGPU material_{};

    // --- 配置 ---
    ViewCubeLayout layout_;
    ViewCubeInteractionState interaction_;
    bool interaction_geometry_dirty_ = false;
    bool initialized_ = false;
};

}  // namespace mulan::engine
