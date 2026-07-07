# Picking / Selection 后续计划

## 当前代码状态

当前项目已经有一条 CPU 侧实体级 picking 链路：

```text
DocWidget
  -> framebufferPosition()
  -> DocumentViewBinding::pickEntityAt()
  -> Camera::screenRay()
  -> RenderScene::pick()
  -> worldBounds AABB intersection
```

相关代码位置：

```text
src/app/ui/doc_widget.cpp
src/app/ui/document_view_binding.cpp
src/view/render_scene.cpp
src/engine/render/camera/camera.h
```

当前能做到：

- 鼠标 hover 时得到实体级 `pickId`；
- 点击时通过 `DocumentViewBinding::selectSingle()` 选中 entity；
- render 层通过 `RenderOptions::hoveredPickId` 驱动边线高亮；
- `RenderScene::PickResult` 已经返回 `entity / pickId / distance`。

当前还没有做到：

- GPU picking；
- face 级 picking；
- part / assembly 分级 selection scope；
- 多选 / 框选；
- 独立 selection render pass；
- outline post process；
- 真实三角面级精确 picking；
- hover tooltip；
- 选中实体整体填充高亮。

## 当前方案的主要限制

### 1. CPU AABB picking 精度有限

`RenderScene::pick()` 当前只对 `proxy.worldBounds` 做 ray-AABB 相交：

```text
ray -> entity worldBounds
```

这会带来两个现象：

- 鼠标点在包围盒空白区域时，也可能命中实体；
- 复杂模型、薄片模型、镂空模型的 picking 会感觉有误差。

短期可以接受，因为当前目标是实体级 hover / select；但如果要做 face 级或 CAD 精确选择，必须升级。

### 2. pickId 仍是实体级

当前 `pickId` 来自：

```text
proxy.entity.index()
```

这适合 entity 级 hover/select，但不够表达：

```text
face
edge
vertex
part
assembly
instance
submesh
```

未来如果要分级选择，需要在导入和 scene proxy 阶段保留更细的 selection id。

### 3. 高亮仍复用 edge path

当前 hover 主要通过 edge draw path 做边线高亮。它适合“鼠标浮上去边线变色”，但不适合：

- 选中后整体实体填充高亮；
- 半透明覆盖；
- silhouette outline；
- 多选对象区分颜色。

这类能力更适合独立 selection/highlight pass。

## 推荐演进顺序

### 阶段 1：收稳当前 CPU picking

目标：让实体级 hover/select 手感可靠。

建议工作：

1. 保持 `Camera::screenRay()` 使用 framebuffer 坐标。
2. 保持 `DocWidget` 中 Qt logical 坐标和 framebuffer 坐标边界清楚。
3. `RenderScene::pick()` 继续返回最近 AABB 命中。
4. 在文档中明确：当前是 entity AABB picking，不是 face picking。

收益：

- 改动小；
- 不影响现有渲染管线；
- 适合当前实体级选择。

### 阶段 2：CPU 三角面 picking

目标：提升点击精度，但仍不引入 GPU picking。

建议工作：

```text
RenderScene::pick()
  -> 先测 worldBounds
  -> 命中后进入 geometry triangles
  -> ray-triangle intersection
  -> 返回 entity / primitive / triangle / distance
```

需要补充的数据：

- `SceneProxy` 中能访问 geometry asset；
- geometry asset 能提供 position/index；
- world transform 用于把 ray 转到 local，或把 triangle 转 world；
- 可选：构建 per-geometry BVH，避免大模型线性扫三角形。

收益：

- 点击精度明显提升；
- 为 face 级 picking 做准备；
- 不需要新增 render pass。

风险：

- 大模型如果不做 BVH，CPU picking 会慢；
- CAD 模型如果拓扑和离散三角面没有建立映射，只能先返回 triangle/submesh，不是真正拓扑 face。

### 阶段 3：Selection Scope

目标：支持 entity / face / part / assembly 分级选择。

建议定义：

```cpp
enum class SelectionScope {
    Entity,
    Face,
    Part,
    Assembly,
};
```

Pick result 应逐步扩展为：

```cpp
struct PickResult {
    scene::EntityId entity;
    uint32_t pickId;
    uint32_t faceId;
    uint32_t partId;
    uint32_t assemblyId;
    double distance;
};
```

导入阶段需要保留：

- glTF node / mesh / primitive / material slot；
- CAD part / face / edge 拓扑 id；
- assembly 层级；
- 稳定 generation，避免缓存错配。

收益：

- 后续自然支持 face/part/assembly 选择；
- 不需要推翻当前 entity 选择；
- selection 语义从一开始就不会被 `pickId = entity.index()` 锁死。

### 阶段 4：GPU picking

目标：当 CPU 三角 picking 或多级 picking 成本过高时，引入 GPU picking。

建议做法：

```text
PickingPass
  -> offscreen integer/id render target
  -> 每个 primitive 输出 encoded selection id
  -> 鼠标位置 readback 1 pixel
  -> decode selection id
```

需要注意：

- readback 要避免每次阻塞 GPU；
- 可以做 1-2 帧延迟；
- selection id 编码要能表达 scope；
- Vulkan/D3D12/WebGPU 后端都应通过 RHI 抽象，不要让 Vulkan 格式泄漏到上层。

收益：

- picking 精度和渲染结果一致；
- 对超大模型更稳定；
- 支持复杂 selection id 编码。

风险：

- 增加 render pass 和 readback 同步复杂度；
- 对多后端 RHI 要求更高；
- 不适合太早做。

### 阶段 5：Highlight / Selection Render Pass

目标：把 hover、selected、multi-select 的显示从普通 edge draw 中解耦。

建议能力：

- hover edge color；
- selected entity fill overlay；
- selected outline；
- multi-select 不同状态颜色；
- hidden-line / wireframe 模式下统一表现。

推荐结构：

```text
SelectionState
  -> selected ids
  -> hovered id
  -> active scope

HighlightPass
  -> consumes MeshDrawCommand + SelectionState
  -> draws selected overlay / outline / edge emphasis
```

收益：

- 高亮表现不污染 FaceStage / EdgeStage；
- 选中和 hover 可以有不同视觉策略；
- 未来 outline post process 可以自然接入。

## 原始待办列表归档

```text
GPU picking
face 级 picking
assembly 级 selection scope
多选 / 框选 / outline post process
独立 selection render pass
边线加粗
hover tooltip
```

## 当前建议

短期先不要直接上 GPU picking。更稳的顺序是：

```text
1. 收稳当前 entity AABB picking
2. 增加 CPU triangle picking / BVH
3. 补 selection scope 数据结构
4. 再做 GPU picking
5. 最后做独立 HighlightPass / outline
```

这样不会让 picking、selection scope、GPU readback、highlight pass 四件事互相缠住。
