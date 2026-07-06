/**
 * @file view_cube_stage.h
 * @brief 导航立方体渲染阶段 — 在视口角落显示方向指示立方体
 * @author hxxcxx
 * @date 2026-05-26
 *
 * 职责：
 *  - 生成带面颜色的导航立方体几何体（6个面中心 + 12个共享边区 + 8个角区）
 *  - 从主相机提取旋转，以正交投影渲染
 *  - 使用视口裁剪将渲染限定在角落小区域
 *
 * 不负责：上层输入分发，只接收并渲染 ViewCubeInteractionState
 */

#pragma once

#include "view_cube_model.h"
#include "../forward/render_stage.h"
#include "../../rhi/buffer.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"
#include "../material/material.h"  // MaterialGPU

#include <cstdint>
#include <array>
#include <memory>

namespace mulan::engine {

class CommandList;
class Sampler;
class TextDrawList;
class Texture;

/// 导航立方体渲染阶段
///
/// 不拥有 PSO / Shader —— 由 Renderer 注入几何绘制执行器的 PSO。
/// 只拥有 CPU 几何体 + 面材质 + 小尺寸 UBO（与主场景布局不同）。
/// render() 从主相机提取旋转、设置角落视口、借用 PSO + BindGroupDesc 录制命令。
class ViewCubeStage final : public RenderStage {
public:
    explicit ViewCubeStage(RHIDevice& device);
    ~ViewCubeStage() override;

    ViewCubeStage(const ViewCubeStage&) = delete;
    ViewCubeStage& operator=(const ViewCubeStage&) = delete;

    std::string_view name() const override { return "ViewCube"; }

    core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void setPipelines(PipelineState* solidPso, PipelineState* edgePso);
    void setFallbackResources(Texture* defaultWhite, Sampler* defaultSampler);

    /// 设置 ViewCube 显示大小（像素）
    void setSize(uint32_t size);

    /// 设置边距（像素，距右下角）
    void setMargin(uint32_t margin);

    void setLayout(const ViewCubeLayout& layout);
    void setInteraction(const ViewCubeInteractionState& interaction);
    void setCorner(ViewCubeCorner corner);
    ViewCubeRect viewportRect(uint32_t vpWidth, uint32_t vpHeight) const;
    ViewCubeHit pick(int screenX, int screenY, uint32_t vpWidth, uint32_t vpHeight) const;
    void collectLabels(TextDrawList& textDraws, const math::Mat4& mainViewMatrix, uint32_t vpWidth,
                       uint32_t vpHeight) const;

    /// 检测屏幕坐标是否在 ViewCube 区域内（交互预留，当前空实现）
    /// @return true 如果在区域内
    bool hitTest(int screenX, int screenY, uint32_t vpWidth, uint32_t vpHeight) const;

    bool isInitialized() const { return initialized_; }

private:
    // --- 内部类型 ---

    /// 立方体顶点格式：pos(3f) + normal(3f) + uv(2f) = 32 bytes（与主场景顶点布局一致）
    struct CubeVertex {
        float pos[3];
        float normal[3];
        float uv[2];
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
    void updateInteractionMaterials();

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
    std::array<uint32_t, ViewCubeModel::kPartCount> part_index_offsets_{};
    std::array<uint32_t, ViewCubeModel::kPartCount> part_index_counts_{};
    std::unique_ptr<Buffer> axis_vb_;
    std::unique_ptr<Buffer> axis_ib_;
    static constexpr uint32_t kAxisCount = 3;
    static constexpr uint32_t kAxisSegments = 16;
    uint32_t axis_index_count_ = 0;

    // --- UBO（ViewCube 的 Scene/Object UB 布局与主场景不同，独立持有）---
    std::unique_ptr<Buffer> scene_ubo_;     // b0 — 正交投影 + 提取旋转
    std::unique_ptr<Buffer> object_ubo_;    // b1 — 单位矩阵
    std::unique_ptr<Buffer> material_ubo_;  // b2 — 26个可交互部件 + 坐标轴材质
    static constexpr uint32_t kPartCount = ViewCubeModel::kPartCount;
    static constexpr uint32_t kAxisMaterialOffset = kPartCount;
    static constexpr uint32_t kMaterialCount = kPartCount + kAxisCount;

    // --- per-frame BindGroup（按借用 PSO 的 layout 创建，缓存在 PSO 不变期间复用）---
    std::unique_ptr<BindGroup> face_bg_;  // solid PSO (10 binding)
    uint64_t face_bg_layout_hash_ = 0;

    // --- 面材质数据 ---
    MaterialGPU materials_[kMaterialCount];
    uint32_t material_stride_ = sizeof(MaterialGPU);

    // --- 配置 ---
    ViewCubeModel model_;
    ViewCubeInteractionState interaction_;
    bool initialized_ = false;
};

}  // namespace mulan::engine
