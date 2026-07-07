# 绘制与夹点编辑框架计划

## 目标

把当前项目从“模型查看器 + entity 级选择”推进到“空视口可绘制线/面/体，并支持夹点编辑”的可演进 CAD 编辑框架。

计划必须贴合当前代码：

- 视口输入入口：`src/app/ui/doc_widget.*`
- 文档/会话入口：`src/app/ui/document_session.*`、`src/io/document.*`
- 交互状态机：`src/engine/interaction/operator.h`
- 视口运行时：`src/view/view_context.*`
- 场景数据：`src/scene/scene.*`
- 几何资产：`src/asset/*`
- 拾取缓存：`src/view/render_scene.*`
- 渲染前端/后端：`src/engine/render/*`

## 当前代码事实

### 已具备

1. `DocWidget` 已把 Qt 鼠标键盘事件转换成 `engine::InputEvent`，并转交 `ViewContext::handleInput()`。
2. `ViewContext` 已有 operator 栈，默认 operator 是 `CameraManipulator`。
3. `Operator` 已有明确的 active/finished/cancelled 生命周期，适合承载绘制命令和编辑命令。
4. `Scene` 已有 entity、transform、geometry、render、selection、bounds 等组件。
5. `Document` 已能创建 `MeshAsset` / `TessellatedAsset` 并挂入 Scene。
6. `RenderScene::pick()` 已支持 entity bounds 过滤后做 CPU triangle picking。
7. `math` 层已有 ray-plane、ray-triangle、point-segment 等基础相交/距离函数。
8. 项目已引入 OCCT，可作为后续 B-Rep 实体编辑内核。

### 不足

1. 当前没有绘制类 `Operator`，`pushOperator()` 没有被 UI 工具使用。
2. 当前 asset 以渲染网格为主，缺少可编辑 sketch/B-Rep 语义。
3. 当前 selection/pick 仍主要是 entity 级，不能表达 face/edge/vertex/grip。
4. 当前没有工作平面、捕捉、动态预览、夹点渲染、撤销重做命令栈。
5. 当前 `TessellatedAsset` 只保存渲染网格，不适合做 CAD 级参数化编辑。

## 总体架构

```text
Qt Ribbon / DocWidget
  -> ViewContext::pushOperator()
  -> Draw/Edit Operator
  -> WorkPlane + SnapManager + PickService
  -> EditOverlay preview / grip render data
  -> DocumentCommand execute()
  -> Document / Scene / AssetLibrary
  -> SketchAsset or BrepAsset
  -> render mesh rebuild
  -> RenderScene::sync()
  -> Renderer
```

核心原则：

1. `Operator` 只管交互流程，不直接塞复杂文档修改逻辑。
2. 文档修改必须通过 command 栈，保证 undo/redo。
3. 渲染网格是可编辑几何的派生缓存，不作为长期编辑真相。
4. 拾取结果必须从 entity 级扩展到 sub-object/grip 级。
5. 先完成 2D sketch 闭环，再做面，最后做 B-Rep 体。

## 模块划分

### 1. 编辑资产层

建议新增：

```text
src/asset/sketch_asset.h
src/asset/sketch_asset.cpp
src/asset/brep_asset.h
src/asset/brep_asset.cpp
```

第一阶段先做 `SketchAsset`：

```cpp
enum class SketchElementKind {
    Point,
    Line,
    Polyline,
    Rectangle,
    Circle,
    Arc,
};

struct SketchElementId {
    uint32_t value = 0;
};

struct SketchLine {
    math::Point3 start;
    math::Point3 end;
};

struct SketchPolyline {
    std::vector<math::Point3> points;
    bool closed = false;
};

class SketchAsset : public GeometryAsset {
public:
    void addLine(const math::Point3& start, const math::Point3& end);
    void addPolyline(std::vector<math::Point3> points, bool closed);
    void rebuildRenderMesh();
    void collectDrawables(std::vector<Drawable>& out) const override;
    math::AABB3 localBounds() const override;
};
```

注意：

- `SketchAsset` 要生成 wire mesh 供现有渲染管线显示。
- 后续面填充可以生成 solid mesh + wire mesh。
- 不要把 sketch 点线只存在 `graphics::Mesh` 里，否则夹点编辑无法追踪语义。

### 2. 工作平面

建议新增：

```text
src/engine/interaction/work_plane.h
src/engine/interaction/work_plane.cpp
```

能力：

```cpp
class WorkPlane {
public:
    static WorkPlane worldXY();
    static WorkPlane worldXZ();
    static WorkPlane worldYZ();
    static WorkPlane fromView(const engine::Camera& camera);

    std::optional<math::Point3> intersectScreen(
        const engine::Camera& camera,
        double screenX,
        double screenY) const;
};
```

第一版默认使用世界 XY 平面，Z=0。

### 3. 捕捉系统

建议新增：

```text
src/engine/interaction/snap_context.h
src/engine/interaction/snap_context.cpp
src/engine/interaction/snap_manager.h
src/engine/interaction/snap_manager.cpp
```

能力：

```cpp
enum class SnapKind {
    None,
    Grid,
    Endpoint,
    Midpoint,
    Center,
    Intersection,
    Nearest,
    Perpendicular,
};

struct SnapResult {
    bool snapped = false;
    SnapKind kind = SnapKind::None;
    math::Point3 worldPoint;
    scene::EntityId entity;
    uint32_t subId = 0;
};
```

第一版优先级：

1. Grid snap
2. Endpoint snap
3. Midpoint snap
4. Nearest on segment

### 4. Operator 绘制命令

建议新增：

```text
src/engine/interaction/draw_line_operator.h
src/engine/interaction/draw_polyline_operator.h
src/engine/interaction/draw_rectangle_operator.h
src/engine/interaction/draw_circle_operator.h
src/engine/interaction/grip_edit_operator.h
```

更理想的做法是新增 `editor` 静态库承载这些依赖文档的工具：

```text
src/editor/
  interaction/
  commands/
  snapping/
  grips/
  overlay/
```

原因：

- `engine` 当前更偏平台无关输入和渲染后端，不应该直接依赖 `io::Document`。
- 绘制工具需要写文档、访问 Scene/AssetLibrary，放 `editor` 更清晰。

第一阶段可以先建 `src/editor`：

```text
editor -> depends on engine, view, scene, asset, io
app -> depends on editor
```

### 5. 文档命令栈

建议新增：

```text
src/io/document_command.h
src/io/document_command.cpp
src/io/document_history.h
src/io/document_history.cpp
```

基本接口：

```cpp
class DocumentCommand {
public:
    virtual ~DocumentCommand() = default;
    virtual bool execute(Document& doc) = 0;
    virtual bool undo(Document& doc) = 0;
};

class DocumentHistory {
public:
    bool execute(std::unique_ptr<DocumentCommand> command, Document& doc);
    bool undo(Document& doc);
    bool redo(Document& doc);
};
```

`DocumentSession` 持有 `DocumentHistory`，UI 的 Undo/Redo action 接到这里。

第一阶段命令：

```text
CreateSketchEntityCommand
UpdateSketchElementCommand
DeleteEntityCommand
TransformEntityCommand
```

### 6. 预览和夹点 Overlay

当前 `ViewState` 已能携带视口状态，后续可以扩展轻量 overlay 数据：

```text
src/view/edit_overlay.h
src/view/edit_overlay.cpp
```

第一版 overlay 数据：

```cpp
struct OverlayLine {
    math::Point3 a;
    math::Point3 b;
    uint32_t color = 0xffffffff;
};

struct OverlayPoint {
    math::Point3 p;
    float size = 6.0f;
    uint32_t color = 0xffffffff;
};

struct EditOverlay {
    std::vector<OverlayLine> lines;
    std::vector<OverlayPoint> points;
};
```

落点：

- `ViewContext` 持有 transient overlay。
- `Operator` 更新 overlay。
- `ViewState` 携带 overlay snapshot。
- `Renderer` 增加 overlay draw path。

第一版也可以复用 line mesh 临时 entity，但长期不推荐，因为 transient preview 不应该污染文档。

### 7. 拾取升级

当前：

```cpp
struct RenderScene::PickResult {
    scene::EntityId entity;
    uint32_t pickId;
    double distance;
};
```

建议演进为：

```cpp
enum class PickScope {
    Entity,
    Face,
    Edge,
    Vertex,
    Grip,
};

struct PickResult {
    PickScope scope = PickScope::Entity;
    scene::EntityId entity;
    uint32_t pickId = 0;
    uint32_t primitiveId = 0;
    uint32_t triangleId = 0;
    uint32_t faceId = 0;
    uint32_t edgeId = 0;
    uint32_t vertexId = 0;
    uint32_t gripId = 0;
    double distance = 0.0;
    math::Point3 worldPoint;
};
```

阶段策略：

1. 先把 triangle id、worldPoint 返回出来。
2. 对 `SketchAsset` 做 edge/vertex/grip picking。
3. 对 `BrepAsset` 做 face/edge/vertex id 映射。
4. 如果 CPU picking 性能不足，再做 GPU picking pass。

### 8. 夹点系统

建议新增：

```text
src/editor/grips/grip.h
src/editor/grips/grip_provider.h
src/editor/grips/sketch_grip_provider.h
src/editor/grips/grip_edit_operator.h
```

基本结构：

```cpp
enum class GripKind {
    Vertex,
    EdgeMidpoint,
    Center,
    Radius,
    FaceCenter,
    Transform,
};

struct Grip {
    uint32_t id = 0;
    GripKind kind = GripKind::Vertex;
    scene::EntityId entity;
    uint32_t ownerSubId = 0;
    math::Point3 worldPosition;
};
```

夹点拖动规则：

- Line：端点 grip 修改 start/end，中点 grip 平移整条线。
- Polyline：顶点 grip 修改对应点，边中点 grip 可插入点。
- Rectangle：角点 grip 改宽高，中心 grip 平移。
- Circle：中心 grip 平移，半径 grip 改 radius。
- Face：轮廓点移动后重新 triangulate。
- B-Rep Body：通过 OCCT 操作重建 shape，不直接移动三角网格。

## 分阶段实施

### 阶段 0：文档和空视口入口

目标：

- UI 可以新建空文档。
- 空文档打开后有一个可绘制视口。
- 不需要导入模型也能进入 `DocWidget`。

文件落点：

- `src/io/document.*`
- `src/app/ui/main_window.*`
- `src/app/ui/document_area.*`

验收：

- 点击 New 后出现空文档 tab。
- 视口可以相机平移/缩放/旋转。
- Fit All 不崩溃。

### 阶段 1：SketchAsset + 画线闭环

目标：

- 默认 XY 工作平面。
- Line 工具可以点两次生成线。
- 鼠标移动有动态预览。
- 完成后生成 Scene entity 并渲染。

文件落点：

- `src/asset/sketch_asset.*`
- `src/editor/interaction/draw_line_operator.*`
- `src/editor/commands/create_sketch_entity_command.*`
- `src/io/document.*`
- `src/app/ui/main_window.*`

验收：

- 空视口画一条线。
- 线可被 entity picking 选中。
- 线显示在 wire/edges 模式下正常。
- Esc 可取消，右键/中键/滚轮不破坏相机操作。

### 阶段 2：多段线、矩形、圆

目标：

- 支持 polyline、rectangle、circle。
- 支持 Enter 完成 polyline。
- 支持 Backspace 撤销当前绘制中的上一个点。
- 支持简单数值输入的预留接口。

验收：

- 四类 sketch element 都能创建。
- render bounds 正确。
- selection 高亮不退化。

### 阶段 3：捕捉

目标：

- Grid snap。
- Endpoint/midpoint snap。
- 最近点 snap。
- 鼠标附近显示 snap marker。

文件落点：

- `src/editor/snapping/*`
- `src/view/edit_overlay.*`

验收：

- 新线起点可以吸附旧线端点。
- 捕捉点显示稳定。
- 高 DPI 坐标仍准确。

### 阶段 4：夹点显示和拖动

目标：

- 选中 sketch entity 后出现夹点。
- 可以拖动线端点、线中点、矩形角点、圆心/半径点。

文件落点：

- `src/editor/grips/*`
- `src/editor/interaction/grip_edit_operator.*`
- `src/view/edit_overlay.*`
- `src/view/render_scene.*`

验收：

- 鼠标命中 grip 优先于 entity。
- 拖动过程中有预览。
- 松开后通过 command 提交，可 undo。

### 阶段 5：面

目标：

- 闭合 polyline 可以生成 face。
- Face 有填充 mesh 和边线 mesh。
- 顶点夹点修改后重新三角化。

可复用：

- `src/math/algo2d/triangulation.h`
- `src/math/algo2d/polygon_intersect.h`

验收：

- 简单凸/凹多边形能显示。
- 拖动点后面填充更新。
- 非法自交轮廓能被拒绝或标红预览。

### 阶段 6：体

目标：

- 支持 Box。
- 支持 Sketch Face 拉伸成实体。
- 使用 OCCT `TopoDS_Shape` 作为真实 B-Rep。
- B-Rep 修改后重新 tessellate 到 `TessellatedAsset` 或 `BrepAsset` 的 render cache。

文件落点：

- `src/asset/brep_asset.*`
- `src/io/brep_builder.*`
- `src/editor/interaction/extrude_operator.*`
- `src/editor/interaction/box_operator.*`

验收：

- 画矩形后可拉伸成盒体。
- 实体可按 face/edge/vertex 拾取。
- 选中体后显示合理夹点。
- Push/Pull face 可作为后续阶段。

## 推荐立即开始的第一个实现切片

第一刀只做“空文档 + 画线最小闭环”，不要同时做夹点、面、体。

具体任务：

1. 新增空文档入口：`MainWindow::New`。
2. 新增 `SketchAsset`，只支持 line。
3. 新增 `Document::addSketchLine(name, start, end)`。
4. 新增 `DrawLineOperator`，使用默认 XY 工作平面。
5. 把 Draw Line action 接到当前 `DocWidget` 的 `ViewContext::pushOperator()`。
6. 暂时用正式 mesh 或轻量 overlay 做动态线预览。
7. 测试：新建文档、画线、选择线、Fit All。

这个切片完成后，后面每个工具都可以复用同一条链。

## 风险和决策点

1. `engine` 是否允许依赖 `io::Document`
   - 建议不允许。
   - 新建 `editor` 模块更干净。

2. preview 第一版走 overlay 还是临时 entity
   - 推荐 overlay。
   - 如果渲染改动成本太高，第一版可以临时 entity，但必须明确后续替换。

3. 体编辑是 mesh 体还是 OCCT B-Rep
   - 只做演示可 mesh。
   - 目标 CAD 编辑必须 B-Rep。

4. picking 是 CPU 还是 GPU
   - 先 CPU。
   - CPU 能满足 sketch 和中小模型。
   - GPU picking 等 sub-object 语义稳定后再上。

## 完成定义

达到“绘制框架可用”的标准：

- 不导入模型也能新建空文档。
- 至少能画 line/polyline/rectangle/circle。
- 绘制过程中有预览和取消。
- 至少支持 grid/endpoint/midpoint 捕捉。
- 选中对象后显示夹点。
- 至少支持 2D sketch 夹点拖动。
- 所有文档修改都能 undo/redo。
- 渲染缓存、Scene dirty、bounds 都正确更新。

达到“面/体可用”的标准：

- 闭合轮廓可生成面。
- 面可被选择并通过顶点夹点编辑。
- Sketch face 可拉伸成 B-Rep 体。
- 体可按 face/edge/vertex 选择。
- 实体修改后能稳定重建 tessellation 和拾取映射。
