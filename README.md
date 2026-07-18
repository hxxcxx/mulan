# Mulan

Mulan（木兰）的想法来自 [Mayo](https://github.com/fougue/mayo.git)。我希望实现一个能够查看多种三维模型，
并可继续扩展编辑与建模能力的跨平台项目。Mulan 使用 C++23 和 Qt 6，
渲染器与 RHI 均在项目中独立实现。

渲染系统起源于我在去年编写的 OpenGL 学习项目
[MulanEngine](https://github.com/hxxcxx/MulanEngine)。它的早期设计十分粗糙，
却成为了 Mulan 的起点；当前的 RHI 与渲染架构已经在此基础上多次重写。

建模层通过 `Shape` handle 和 `IShapeOps` 隔离具体几何内核。OCCT 提供 B-Rep
文件读取和基础建模操作；实验性的
[truck-bridge](https://github.com/hxxcxx/truck-bridge.git) 则由 AI 完成，
通过 C ABI 将 Rust 几何建模库 Truck 接入 C++。

数学模块由我之前使用 ImGui 学习计算几何与 CAGD 时编写的
[BeyondConvex](https://github.com/hxxcxx/BeyondConvex) 和
[BeyondFlat](https://github.com/hxxcxx/BeyondFlat) 演进而来，包含 Bezier、B-Spline、
NURBS 参数曲线与曲面、二维计算几何以及 KD-tree、R-tree、BVH 等实现。

AI 改变了许多事情。帮助我理解陌生领域、梳理思路和验证实现，让过去难以独自推进的想法有机会落地。

借《论语·子罕》改一句：“仰之弥高，钻之弥坚。AI 循循然善诱人，博我以文，
约我以理，欲罢不能。”

## 当前实现

### 平台与渲染

- 支持 Windows x64 和 Linux x64（X11/XCB）。
- 通过统一 RHI 支持 OpenGL、Vulkan、D3D11 和 D3D12。
- 支持多文档标签界面，每个文档拥有独立的 `DocumentSession`。
- 每个文档视图通过 `RenderChannel` 接入 `RenderThread`。
- 配置兼容的非 OpenGL 视图共享 `RenderThread` 和 RHI Device。
- OpenGL 视图保持 OpenGL context 与执行线程隔离。
- Forward Renderer 支持 Shaded、Shaded with Edges 和 Wireframe。
- 支持 selection/hover highlight、交互式 ViewCube 和可配置的 MSAA。
- Material system 实现金属度-粗糙度 PBR workflow。
- 支持 Base Color、Normal、Metallic-Roughness、Emissive 和 AO texture slots。
- 支持 Directional Light、Point Light 和 Spot Light。
- 提供可选的 IBL baking path，默认不启用。
- 渲染链路负责场景同步和 GPU 资源生命周期管理。
- 使用 scene-level Dynamic BVH 和 per-asset primitive index 加速 picking。
- 相机支持 Turntable（Z-up 约束）与 Trackball（四元数自由旋转）两种旋转模式。

### 编辑与建模

- 绘制与编辑交互基于 Operator stack。
- 活动工具取得输入所有权，并可将导航输入委托给 camera operator。
- 临时预览、snapping 和 grips 不直接修改 Document。
- 只有 commit 后的 `DocumentOperation` 才会进入 undo/redo history。
- 新建空白文档提供 Line、Polyline、Circle 和 Face tools。
- 提供 Bezier、B-Spline 和 NURBS 参数曲线工具。
- 可对选中的 planar closed profile 执行 Extrude 生成 solid。
- 当前编辑操作包括 Move、Copy、Delete 和 grip-based geometry editing。
- 建模层对外暴露与后端无关的 `Shape`、文件读取接口和 `IShapeOps`。
- OCCT 与实验性 Truck 通过 runtime-loadable modeling backend plugins 接入。
- Truck 后端当前仅在 Windows 上可选启用。

### 模型导入与工程

- 内置轻量级 CPU 性能埋点、统计与查看界面，并可切换到 Tracy 查看跨线程时间线和调用区间。
- 使用 fastgltf 导入 glTF/GLB。
- 使用 Assimp 导入 OBJ、FBX、DAE、3DS、PLY、STL、
  BLEND、X、ASE、LWO、OFF 和 DXF。
- 通过 OCCT 导入 STEP/STP 和 IGES/IGS B-Rep 模型。
- 使用 CMake Presets 和 vcpkg manifest 统一管理构建与第三方依赖。

Windows 使用 OCCT 7.9.x 官方预编译包，Linux 使用系统 OCCT 7.6.x 开发包。
两端共享相同的建模公共契约，具体内核类型不会泄漏到上层模块。

## 致谢

特别感谢 [terry-chao](https://github.com/terry-chao) 和
[zmb998](https://github.com/zmb998)，他们贡献了 OpenGL 与 Direct3D 11 后端的大部分实现。
不然连现在这个拙劣的 RHI 都不会有，只会存在一个拙劣的 Vulkan，
或许还会有我的第一版 GL——谁知道呢。

## 构建

环境准备、Windows/Linux 构建命令、测试与运行方式请参阅
[Mulan 构建指南](BUILD.md)。
