# 文档能力与命令描述计划

## 当前判断

这组内容目前不是主线优先级。当前已经完成的规则是：

- 新建文档是 Draft 文档，显示绘制命令。
- 打开 / 导入文档是 Imported 文档，不显示绘制命令。
- 导入文档仍然允许选择、移动和复制实体。

后续如果命令、文档类型、草图层、导入模型编辑副本继续增加，需要把这些规则从零散判断升级为正式能力系统。

## 文档能力系统

- 将 `DocumentSessionKind` 继续保留为文档来源 / 模式语义。
- 在 `DocumentSession` 上增加能力位，而不是让 UI 直接判断文档类型。
- 初步能力可以包括：
  - `CanSelect`
  - `CanTransform`
  - `CanDraw2D`
  - `CanDrawSurface`
  - `CanShowDrawTools`
  - `CanEditGeometry`
  - `CanSaveAsDraft`
- Draft 文档默认开启绘制和编辑能力。
- Imported 文档默认开启选择、移动、复制能力，但关闭绘制入口。
- 后续可以增加 Draft Overlay / Edit Copy 模式，让导入模型进入可绘制或可编辑状态。

## 命令描述系统

- `CommandState` 已经有 enabled / visible，后续可以补齐命令描述：
  - `category`
  - `group`
  - `priority`
  - `icon`
  - `shortcut`
  - `statusText`
- Ribbon 不再手写每个 QAction，而是根据命令描述生成分组。
- 命令是否显示、是否启用，只依赖 `CommandHost` 和文档能力。
- Move / Copy / Undo / Redo / Draw 等命令可以共享同一套刷新机制。

## 导入文档编辑模式

- 打开模型默认不显示绘制命令，保持查看 / 选择 / 变换的清晰语义。
- 如果需要在导入模型上绘制，应通过显式模式进入：
  - 新建草图层。
  - 创建编辑副本。
  - 在模型表面建立辅助工作平面。
  - 创建截面线、投影线或参考曲线。
- 这样可以避免“导入模型本身”和“用户新增草图几何”混在同一层。

## 状态刷新事件系统

- 当前已经有 `commandStateInvalidated` 这类轻量通知。
- 后续如果 UI 状态继续复杂，应建立正式事件：
  - `DocumentChanged`
  - `SelectionChanged`
  - `ToolChanged`
  - `HistoryChanged`
  - `ViewChanged`
- MainWindow / Ribbon 只订阅事件并刷新命令状态，不直接理解编辑器内部细节。

## 暂存原因

这组计划主要是 UI / 命令架构优化，不直接解决当前绘制、选择、夹点、面和曲线编辑的核心能力问题。

短期主线仍应优先推进几何编辑能力、选择语义、夹点 / Gizmo、面 / 体建模和下层渲染提交质量。
