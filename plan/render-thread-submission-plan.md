# 渲染线程隔离与提交层重构计划

## 结论

现阶段不建议直接把当前渲染流程搬到独立线程。

真正要先完成的是“渲染提交层”：由 UI / editor / view 主线程把当前场景、预览、高亮、相机和渲染选项整理成不可变的 `RenderSubmission`，渲染端只消费这个提交对象，不再直接读取 `RenderScene`、`AssetLibrary`、`PreviewLayer` 等会被编辑器继续修改的数据结构。

这一步完成后，当前同步渲染路径仍然可以保持不变；未来接入真实渲染线程时，只需要把 `RenderSubmission` 放进队列，而不是重新拆整个 view / engine。

## 当前现状

当前渲染链路大体是：

```text
ViewContext
  -> RenderRuntimeHost
  -> RenderRuntime
  -> Renderer
  -> engine::RenderRenderer
```

其中 `Renderer` 仍然持有并读取这些外部对象：

- `RenderScene`
- `AssetLibrary`
- `PreviewLayer`

`Renderer::render()` 内部会调用 `syncEngineWorld()`，再从上述对象同步出 `RenderWorld`、`RenderWorldSnapshot` 和资源准备列表。

这在单线程同步渲染时可以工作，但如果直接引入渲染线程，会出现明显隐患：

- 编辑器线程可能正在修改实体、几何资产、预览对象或选择状态。
- 渲染线程同时读取这些对象会产生数据竞争。
- GPU 资源准备、surface resize、readback、IBL 加载等生命周期动作没有统一命令边界。
- `RenderThread` / `RenderQueue` 虽然已经存在，但目前还没有接入主渲染路径，不能直接承担完整运行时隔离。

当前已经具备的好基础是：

- `RenderWorldSnapshot` 已经是面向渲染后端的不可变 CPU 快照。
- `RenderResourcePrepareList` 已经能表达本帧需要准备的几何、材质、纹理等资源。
- `PreviewLayer` 已经支持 referenced preview，可以避免大型 transform 预览复制 mesh。
- `RenderRuntimeHost` 已经是接入渲染线程前的策略边界，适合作为同步 / 异步渲染切换点。

## 工业级边界原则

### 1. 渲染线程不能读取编辑器活对象

渲染线程未来不应该直接读：

- document entity
- editable geometry asset
- editor selection state
- hover / grip / gizmo runtime state
- preview layer mutable container
- UI QAction / command state

它应该只读本帧提交出来的不可变数据。

### 2. 渲染对象生命周期归渲染运行时

这些对象应该最终只在渲染运行时线程内创建、更新和释放：

- RHI device
- swapchain / surface binding
- GPU geometry buffer
- material / texture cache
- render pipeline / shader state
- readback / capture staging resource

主线程只能提交“我要渲染什么”和“我要执行什么生命周期命令”，不能直接操作 GPU 资源。

### 3. 提交数据和生命周期命令分开

渲染提交层应该分两类：

- Frame submission：本帧画面需要的不可变快照。
- Render command：resize、shutdown、capture、IBL 更新、surface 重建等生命周期动作。

不要把 resize / capture 伪装成普通帧数据，也不要让 frame submission 承担资源生命周期职责。

### 4. 同步路径先收口，再引入异步

第一阶段仍然同步渲染：

```text
ViewContext
  -> build RenderSubmission
  -> RenderRuntime::render(submission)
```

第二阶段才切换为：

```text
ViewContext
  -> build RenderSubmission
  -> RenderQueue::push(submission)
  -> RenderThread consumes submission
```

这样风险最小，也方便每一步构建验证。

## 目标结构

目标链路应该逐步变成：

```text
Document / Editor / View mutable state
  -> RenderSubmissionBuilder
  -> RenderSubmission
  -> RenderRuntimeHost
  -> RenderRuntime
  -> Renderer
  -> engine::RenderRenderer
```

其中：

- `RenderSubmissionBuilder` 属于 view 层，负责从 view 当前状态构建渲染提交。
- `RenderSubmission` 是不可变帧数据，未来可以跨线程移动。
- `Renderer` 不再持有 `RenderScene`、`AssetLibrary`、`PreviewLayer` 指针。
- `RenderRuntime` 负责调度渲染、资源准备、surface 状态。
- `engine::RenderRenderer` 保持底层渲染后端职责，不理解 editor 活对象。

## 阶段一：RenderSubmission 同步收口

### 目标

在不引入真实渲染线程的前提下，先把渲染输入收成单一提交对象。

### 建议新增类型

`RenderSubmission`：

- `engine::RenderWorldSnapshot world`
- `engine::RenderResourcePrepareList prepare`
- `ViewState view`
- `RenderWorldSyncStats sync_stats`
- `uint64_t generation`
- `bool valid`

`RenderSubmissionBuilder`：

- 持有 `RenderWorldSync`
- 持有用于构建快照的 `engine::RenderWorld`
- 接收 `RenderScene`、`AssetLibrary`、`PreviewLayer`
- 负责执行原本 `Renderer::syncEngineWorld()` 的职责
- 输出 `RenderSubmission`

### 应完成的代码变化

- 从 `Renderer` 中移除对 `RenderScene`、`AssetLibrary`、`PreviewLayer` 的长期依赖。
- 把 `Renderer::syncEngineWorld()` 的职责迁移到 `RenderSubmissionBuilder`。
- `Renderer::render()` 改为消费 `const RenderSubmission&`。
- `RenderRuntime::render(const ViewState&)` 内部先通过 builder 构建 submission，再传给 renderer。
- `RenderRuntimeHost` 的外部接口暂时可以保持不变，降低上层改动面。

### 验收标准

- 当前同步渲染行为不变。
- 普通实体、hover、selected、preview、snap、grip、referenced transform preview 都能继续显示。
- `Renderer` 不再直接读编辑器 / view 活对象。
- 渲染后端只面对快照和渲染请求。

## 阶段二：渲染生命周期命令化

### 目标

把不属于普通帧提交的动作收成正式命令，为未来线程化做准备。

### 命令范围

- `ResizeSurfaceCommand`
- `ShutdownRendererCommand`
- `EnableIblCommand`
- `ConfigureCaptureSurfaceCommand`
- `ConfigureOffscreenSurfaceCommand`
- `ReadbackPixelsCommand`

### 关键规则

- resize 不能和普通帧提交混在一起隐式执行。
- readback / capture 可以阻塞等待，但必须通过明确 fence 或 future 表达。
- shutdown 必须排在队列末尾并等待渲染端释放资源。
- 命令执行侧归 `RenderRuntime`，主线程只提交命令。

### 验收标准

- 同步路径下命令仍然立即执行。
- API 语义开始接近未来异步路径。
- 没有新增隐藏式跨线程访问。

## 阶段三：接入真实 RenderThread

### 目标

在提交层和命令层完成后，再让 `RenderRuntimeHost` 选择同步或异步运行策略。

### 建议结构

```text
RenderRuntimeHost
  -> SyncRenderRuntimeStrategy
  -> ThreadedRenderRuntimeStrategy
```

同步策略：

- 直接调用 `RenderRuntime`。
- 用于当前开发、测试、诊断和单线程 fallback。

异步策略：

- 持有 `RenderThread`。
- 主线程提交 `RenderSubmission` 和 `RenderCommand`。
- 渲染线程拥有 `RenderRuntime`、device、surface 和 GPU cache。

### 帧提交策略

第一版建议使用“只保留最新帧”的队列：

- 主线程连续提交多帧时，可以丢弃过期 frame submission。
- 生命周期命令不能丢弃，必须按顺序执行。
- resize 后的旧帧应被丢弃或带 generation 检查。

### 验收标准

- UI 线程不等待普通渲染帧完成。
- resize、shutdown、capture 行为稳定。
- 渲染线程不直接访问 document / editor / preview 的可变对象。
- 可以通过同步策略快速退回排查问题。

## 阶段四：面向未来的 Render Graph

这不是当前必须立刻完成的内容，但提交层应该预留方向。

未来可以在 engine 内部把一帧拆成更明确的 pass：

- depth prepass
- opaque pass
- edge pass
- highlight pass
- overlay pass
- gizmo pass
- readback / picking pass

这个阶段的目标不是为了“看起来像高级引擎”，而是为了让资源依赖、pass 顺序、临时纹理和 readback 变得可分析、可调度、可调试。

## 不建议现在做的事

- 不建议直接把 `Renderer::render()` 放到后台线程运行。
- 不建议让渲染线程继续读取 `RenderScene` / `PreviewLayer` 指针。
- 不建议先改 UI 或 editor 工具来适配线程。
- 不建议让每个工具自己提交渲染命令。
- 不建议为了线程化牺牲当前同步路径的可调试性。

## 推荐实施顺序

### Commit 1：提交层同步收口

- 新增 `RenderSubmission`。
- 新增 `RenderSubmissionBuilder`。
- 迁移 `Renderer::syncEngineWorld()`。
- `Renderer` 改为消费 submission。
- 保持外部行为不变。

### Commit 2：生命周期命令边界

- 抽象 resize / shutdown / capture / IBL / readback 命令。
- 同步路径先直接执行命令。
- 保证命令和 frame submission 分离。

### Commit 3：运行时策略边界

- 在 `RenderRuntimeHost` 内建立 sync / threaded strategy 边界。
- 默认仍使用 sync strategy。
- 为 threaded strategy 预留队列和启动关闭语义。

### Commit 4：真实线程接入

- 让 threaded strategy 拥有 `RenderThread` 和 `RenderRuntime`。
- 帧提交使用 latest-frame 策略。
- 生命周期命令按顺序执行。
- 补齐 shutdown / resize / capture 等同步点。

## 当前下一步

最应该马上做的是 Commit 1。

这一步价值最高，因为它不改变用户可见行为，却能把未来渲染线程最大的风险提前消掉：渲染端不再读取编辑器活对象。只要这一步完成，后续是否开线程、怎么排队、怎么处理 readback，都会变成可控的运行时策略问题，而不是全项目数据竞争问题。
