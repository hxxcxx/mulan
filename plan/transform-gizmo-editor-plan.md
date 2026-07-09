# Transform / Gizmo 编辑闭环计划

## 当前已完成

- 实体级 TransformTool 已具备移动预览、实体移动提交、复制移动提交。
- 预览层当前通过世界坐标临时 mesh 显示移动结果，能复用曲线、面、Mesh、Tessellated 几何的统一 drawable 展开。
- TransformEditContext 已能从 SelectionTarget 收集实体级初始 world transform，并为未来子对象变换保留 subject 结构。

## 后续优先事项

### 1. Gizmo 操作器层

- 建立统一 Gizmo 对象，支持平移、旋转、缩放三类操控柄。
- Gizmo hit test 走独立 pick identity，不污染普通几何选择。
- Gizmo 拖动复用 TransformTool / TransformEditContext，不再为每种夹点各写一套拖动管线。
- Gizmo 视觉层单独管理轴、平面手柄、中心点和 hover/active 状态。

### 2. 预览提交层升级

- 将当前 “CPU 拷贝 mesh 到世界坐标” 的预览升级为 “geometry handle + preview transform”。
- 对大模型拖动避免每帧复制顶点，只更新临时对象 transform。
- 支持预览时隐藏或弱化原对象，让 move 和 copy 的视觉语义更清楚。
- 为旋转、缩放、阵列、镜像等变换复用同一套 preview submission。

### 3. Undo / Redo 事务

- DocumentOperation 增加可逆命令记录。
- Move 保存 before/after transform，Copy 保存新实体 id，Delete 保存被删实体快照。
- 以一次工具提交作为一个事务，不把鼠标移动过程写进历史栈。
- UI 的 Undo / Redo 按钮接入真实事务栈。

### 4. 子对象 Transform

- SelectionTarget 已能表达面、边、点、控制点等子对象，下一步需要为 TransformEditSubject 增加子对象编辑 payload。
- 曲线控制点、多选控制点、面顶点、Mesh 顶点走统一 sub-object transform 提交。
- 子对象变换提交必须区分“修改资产几何”与“修改实体 transform”，避免两个层级混用。

### 5. 精确编辑输入

- Move / Copy 支持命令行式距离输入、XYZ 增量输入、目标点输入。
- 轴约束、snap、work plane 在 TransformTool 中保持同一套输入解析。
- 后续旋转支持角度输入，缩放支持比例输入。

### 6. 属性与复制策略

- Copy 当前共享几何资产，后续需要提供 instance copy / deep copy 两种策略。
- 属性面板显示 transform、复制策略、实例来源。
- 对可编辑曲线和未来 BRep，需要支持 “编辑前 make unique asset”。

### 7. 选择状态与高亮协同

- Move 时选中对象、预览对象、原对象的高亮关系需要显式规则。
- Copy 提交后是否选中新副本需要成为工具策略。
- 多选状态下统一 hover、selected、active transform subject 的视觉优先级。
