/**
 * @file view_cube_renderer.h
 * @brief 导航立方体渲染器 — 在视口角落显示方向指示立方体
 * @date 2026-05-26
 *
 * 职责：
 *  - 生成带面颜色的立方体几何体（6面 + 12边）
 *  - 从主相机提取旋转，以正交投影渲染
 *  - 使用视口裁剪将渲染限定在角落小区域
 *
 * 不负责：点击交互（预留接口，空实现）
 */

#pragma once

#include "../../rhi/device.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/buffer.h"
#include "../../rhi/shader.h"
#include "../camera/camera.h"

#include <cstdint>
#include <memory>

namespace mulan::engine {

class CommandList;

/// ViewCube 面枚举（用于交互预留）
enum class ViewCubeFace : uint8_t {
    Front  = 0,
    Back   = 1,
    Left   = 2,
    Right  = 3,
    Top    = 4,
    Bottom = 5,
};

/// 导航立方体渲染器
class ViewCubeRenderer {
public:
    explicit ViewCubeRenderer(RHIDevice* device);
    ~ViewCubeRenderer();

    ViewCubeRenderer(const ViewCubeRenderer&) = delete;
    ViewCubeRenderer& operator=(const ViewCubeRenderer&) = delete;

    /// 初始化：创建 PSO、UBO、几何缓冲
    /// @param colorFmt 渲染目标颜色格式
    /// @param depthFmt 深度缓冲格式
    bool init(TextureFormat colorFmt, TextureFormat depthFmt);

    /// 释放资源
    void cleanup();

    /// 渲染 ViewCube（在 Renderer::render() 末尾调用）
    /// @param cmd            命令列表
    /// @param mainViewMatrix 主相机 view 矩阵（提取旋转）
    /// @param vpWidth        视口总宽度
    /// @param vpHeight       视口总高度
    void render(CommandList* cmd, const Mat4& mainViewMatrix,
                uint32_t vpWidth, uint32_t vpHeight);

    /// 设置 ViewCube 显示大小（像素）
    void setSize(uint32_t size) { cube_size_ = size; }

    /// 设置边距（像素，距右下角）
    void setMargin(uint32_t margin) { margin_ = margin; }

    /// 检测屏幕坐标是否在 ViewCube 区域内（交互预留，当前空实现）
    /// @return true 如果在区域内
    bool hitTest(int screenX, int screenY,
                 uint32_t vpWidth, uint32_t vpHeight) const;

    /// 获取点击的面（交互预留，当前空实现）
    /// @return std::nullopt
    std::optional<ViewCubeFace> hitTestFace(int screenX, int screenY,
                                             uint32_t vpWidth, uint32_t vpHeight) const;

    /// 跳转到指定面（交互预留，当前空实现）
    void snapToFace(ViewCubeFace face, Camera& camera);

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

    // --- SceneUBO / ObjectUBO / MaterialUBO 与主场景相同布局 ---
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

    struct alignas(16) MaterialUBO {
        float baseColor[3];    float metallic;
        float emissive[3];     float roughness;
        float specular[3];     float shininess;
        float alpha;           float ao;
        float emissiveStr;     float alphaCutoff;
        uint32_t materialType; uint32_t alphaMode;
        uint32_t textureFlags; uint32_t doubleSided;
    };
    static_assert(sizeof(MaterialUBO) == 80);

    // --- 内部方法 ---
    bool createGeometry();
    bool createFaceGeometry();
    bool createEdgeGeometry();

    // --- 设备 ---
    RHIDevice*   device_;

    // --- PSO（复用主场景 solid/edge shader，独立 PSO 实例）---
    std::unique_ptr<Shader>         solid_vs_;
    std::unique_ptr<Shader>         solid_fs_;
    std::unique_ptr<PipelineState>  solid_pso_;
    std::unique_ptr<Shader>         edge_vs_;
    std::unique_ptr<Shader>         edge_fs_;
    std::unique_ptr<PipelineState>  edge_pso_;

    // --- 几何缓冲 ---
    std::unique_ptr<Buffer>         face_vb_;     // 面顶点
    std::unique_ptr<Buffer>         face_ib_;     // 面索引
    uint32_t                    face_index_count_ = 0;
    std::unique_ptr<Buffer>         edge_vb_;     // 边顶点
    std::unique_ptr<Buffer>         edge_ib_;     // 边索引
    uint32_t                    edge_index_count_ = 0;

    // --- UBO ---
    std::unique_ptr<Buffer>         scene_ubo_;   // b0
    std::unique_ptr<Buffer>         object_ubo_;  // b1
    std::unique_ptr<Buffer>         material_ubo_;// b2（6个面各一份）
    static constexpr uint32_t   kFaceCount = 6;

    // --- 面材质数据 ---
    MaterialUBO                 face_materials_[kFaceCount];
    uint32_t                    material_stride_ = sizeof(MaterialUBO);

    // --- 配置 ---
    uint32_t                    cube_size_ = 128;
    uint32_t                    margin_   = 16;
    bool                        initialized_ = false;

    // --- 暂存全屏视口用于恢复 ---
    Viewport                    saved_viewport_;
    ScissorRect                 saved_scissor_;
};

} // namespace mulan::engine
