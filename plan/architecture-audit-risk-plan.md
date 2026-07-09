# 绘制编辑框架架构体检与风险清单

## 结论

当前框架不建议推倒重构。

最近几轮改动已经把关键方向打出来了：工具输入、拾取、捕捉、临时可视、选择高亮、几何资产 mutation、Undo / Redo、Transform 提交都有了正式入口。现在真正需要控制的是“中间协调层继续膨胀”的风险，而不是重新写一套绘制系统。

下一阶段如果继续动架构，优先级应该是拆薄现有边界，而不是新增几何能力。

## 当前健康的部分

### 1. 模块依赖方向基本成立

当前 CMake 目标大体保持了下面的方向：

- `app` 负责 UI、文档窗口、编辑器工具和操作提交。
- `view` 负责视图上下文、相机、预览层、文档渲染缓存与渲染运行时接入。
- `engine` 负责渲染前端、渲染后端、交互基础设施和高亮 pass。
- `asset` / `scene` 负责资产与场景数据。
- `io` 负责文档加载、保存和文档编辑入口。

没有看到必须立刻处理的 target 环形依赖。

### 2. 编辑器输入已经有正式上下文

`EditorInputResolver`、`EditorPickService`、`EditorOverlayService` 已经把工具输入、拾取、临时可视从具体工具里拆出来。未来线、面、体、参数曲线、snap、work plane 都可以继续从这里扩展。

这部分方向正确，后续应继续保持“工具只消费上下文，不自己到处算射线和坐标”。

### 3. 高亮已经脱离普通绘制阶段

`SelectionVisualState` 和 `HighlightStage` 已经把 hover / selected 从普通 Face / Edge 绘制里拆出来。实体高亮、未来子对象高亮、控制点高亮可以继续在这个语义层扩展。

这里不需要回退到“修改普通材质颜色”的方式。

### 4. 几何修改已经有统一提交入口

`GeometryMutation` / `GeometryEditService` 已经把曲线和面的几何资产修改接入 `DocumentOperation`，并接入 Undo / Redo。make unique 机制也已经开始保护共享几何资产。

这让后续曲线控制点编辑、面顶点编辑、体的边面点编辑都能走同一套 mutation。

### 5. Transform 编辑已经开始闭环

实体移动、复制、预览、提交、Undo / Redo 已经在统一变换提交链路里。后续 Gizmo、夹点拖动、轴约束和数值输入应该继续复用这条链路。

## 主要风险

### P0. `DocumentViewBinding` 职责过宽

当前 `DocumentViewBinding` 同时承担：

- 绑定 `DocumentSession` 和 `ViewContext`。
- 管理 `DocumentRenderCache`。
- 同步并注入 `RenderScene`。
- 应用视图偏好和相机裁剪面。
- 执行拾取。
- 修改文档选择。
- 触发 refresh。

它现在是 app / view / document / render cache / selection 的交界处。短期还能用，但继续叠加子对象选择、局部刷新、异步渲染或复杂拾取后，它会成为最容易出错的中心点。

建议后续优先拆成：

- `DocumentRenderBinding`：只负责文档到 render cache / render scene 的同步。
- `DocumentPickBridge`：只负责从 View + RenderScene 得到 pick result。
- `DocumentSelectionBridge`：只负责文档选择和视图选择状态同步。
- `DocumentViewBinding` 保留为轻量 facade，避免一次性大迁移。

### P0. `EditorSession` 仍然是编辑器总控中心

`EditorSession` 目前已经挂了：

- `EditorInputResolver`
- `EditorPickService`
- `EditorOverlayService`
- `DocumentOperationExecutor`
- `EditorGripController`
- `EditorSelectionContext`
- `ToolController`

这比工具自己乱算好很多，但它仍然在处理输入、选择、hover、夹点、工具生命周期、预览、操作提交和高亮同步。后续如果继续加曲线编辑器、面编辑器、Gizmo、子对象选择，它会再次变成巨型类。

建议后续拆分顺序：

1. `SelectionService`：集中处理 hover、selection、selection visual state。
2. `ToolHost`：集中处理工具生命周期和输入分发。
3. `EditorCommandBridge`：集中处理 undo / redo / move / copy / tool command 的状态查询。
4. `EditorSession` 只保留组合和生命周期管理。

### P1. Overlay 目前适合小型预览，不适合大型变换预览

`PreviewLayer` 当前以曲线和 mesh 形式收集 `Tool` / `Snap` / `Grip` / `GripHot` 临时可视对象，并在视图侧构建预览 drawable。

这对捕捉符号、夹点、小圆片、辅助线是合理的。但未来实体移动预览、复杂面编辑预览、体操作预览如果也复制 mesh，会带来 CPU 重建和显存资源抖动。

建议未来引入两类 overlay：

- Immediate overlay：小型符号、辅助线、snap marker，继续走当前 `PreviewLayer`。
- Referenced overlay：引用已有 entity / geometry handle，加临时 transform / material override，不复制几何。

### P1. Pick 语义已经接近临界点

`RenderScene::PickResult` 和 `SelectionVisualTarget` 已经包含 entity、drawable、primitive、curve segment、mesh face、control point 等信息。这是必要的，但也意味着 render scene 开始承载编辑语义。

风险不是“字段多”，而是未来工具可能直接依赖渲染内部字段，导致 pick identity 和 render item layout 强耦合。

建议保持两个层次：

- View / engine 返回稳定的低层 pick identity。
- App / editor 通过 `EditorPickService` 转换为 `SelectionTarget` / `EditorPickHit`。

工具不应该直接消费 `RenderScene::PickResult`。

### P1. Geometry mutation 的共享资产与 Undo 需要更强不变量

`GeometryEditService` 已经支持 make unique 和 undo mutation，这是正确方向。但几何资产共享、撤销后移除临时 unique asset、redo 后重建 undo mutation 这类逻辑天然容易出现边界问题。

后续继续加曲线插点、删除点、NURBS 权重、面轮廓重建前，应该补明确不变量：

- mutation apply 前后 entity 的 geometry asset 必须可预测。
- make unique 只在共享资产时发生。
- undo 不应该误删仍被其他实体引用的资产。
- redo 重新生成的 undo mutation 必须以当前资产为准。

这部分最好先用测试保护，再扩展复杂编辑工具。

### P1. 子对象选择还缺稳定的语义层

实体级选择已经能工作，子对象高亮也预留了 domain。但未来点、边、面、控制点、grip、gizmo handle 都需要一个统一身份，不然会出现“看得到高亮，但不知道编辑谁”的情况。

建议最终沉淀一个 `SelectionTarget` 语义层，包含：

- entity identity。
- sub-object domain。
- component index / curve element id / face loop id。
- optional transform space。
- optional edit asset id。

这层应该属于 editor / app，而不是直接属于 renderer。

### P2. `ViewContext` 很宽，但暂时不是最危险点

`ViewContext` 同时处理 render runtime、相机、operator、capture、preview layer、view cube、IBL、selection visual state。它确实很宽，但大部分职责仍然属于“视图运行时”范围。

短期不建议优先拆它。除非开始做多视图、多窗口、后台渲染线程或复杂视图布局，否则先拆 `DocumentViewBinding` 和 `EditorSession` 收益更高。

### P2. CMake / 运行时部署仍影响开发体验

当前已经做过 DLL 筛选和部署整理，但仍要警惕：

- configure 阶段不应递归扫描过大第三方目录。
- public headers copy 不应每次刷新时间戳。
- app target 的部署逻辑过长，后续可以继续拆到 helper cmake。
- 本机环境里的 `PATH` / `Path` 重复问题仍可能影响构建命令。

这不是绘制框架主风险，但会影响“随时删 build、F5 无感启动”的体验。

## 不建议现在做的事

- 不建议推倒重写 Editor / View / Engine。
- 不建议继续堆新绘制工具来验证架构。
- 不建议立刻做完整子对象高亮 pass。
- 不建议现在做复杂体建模、布尔、参数历史。
- 不建议把 Gizmo、Grip、曲线控制点编辑分别写成三套拖动管线。

## 建议下一步

如果下一次继续架构工作，推荐只做一个高价值拆分：

### 拆薄 `DocumentViewBinding`

目标不是改行为，而是让职责边界变清楚：

1. 抽 `DocumentRenderBinding`，只管理 `DocumentRenderCache`、render scene sync、inject render cache。
2. 抽 `DocumentPickBridge`，只处理相机 + framebuffer 坐标到 pick result。
3. 抽 `DocumentSelectionBridge`，只处理 document selection 和 view selection visual 的同步入口。
4. 保留 `DocumentViewBinding` 作为兼容 facade，逐步迁移调用方。

这一步做完，后续再拆 `EditorSession` 会顺很多。

## 后续路线

1. `DocumentViewBinding` 拆薄。
2. `GeometryEditService` 不变量与测试保护。
3. `SelectionTarget` 编辑语义层收紧。
4. Overlay 区分 immediate / referenced 两类提交。
5. `EditorSession` 拆出 `SelectionService` / `ToolHost`。
6. 再继续曲线控制点编辑、面编辑、Gizmo、体建模等功能。

## 当前状态判断

现在的框架已经从“能画”进入“有架构骨架”的阶段，但还不是工业级稳定形态。

最合理的节奏是：不要推倒，不急着加功能，先把最胖的连接层拆薄。这样未来每一个新增工具都能挂到清晰边界上，而不是重新把复杂度塞回工具和视图里。
