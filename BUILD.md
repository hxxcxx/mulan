# Mulan 构建指南

本文档描述当前仓库支持的全部构建方式，以及项目公开的每一个 CMake 配置变量。命令默认在仓库根目录执行，并以 PowerShell 为例。

> 当前平台层、建模后端和运行时部署只支持 Windows。顶层 CMake 在非 Windows 平台会直接停止配置。

## 1. 构建环境

### 1.1 必需工具

| 工具/依赖 | 当前要求 | 用途 |
| --- | --- | --- |
| Git | 支持子模块的版本 | 获取源码及 `3rdparty` 子模块 |
| CMake | 3.24 或更高 | 配置、构建和测试；Preset 文件格式为 version 6 |
| Visual Studio | 安装可提供 `Visual Studio 18 2026` 生成器的版本，并包含 MSVC x64 C++ 工具链 | 当前 `msvc` Preset 固定使用该生成器和 x64 架构 |
| vcpkg | 与 `vcpkg.json` 的 baseline 兼容 | 以 manifest 模式安装 Qt、Vulkan、GTest、Slang 等依赖 |
| OpenCASCADE | 7.9.x，Windows vc14 64 位目录布局 | 默认 OCCT 建模后端；配置时拒绝 7.9.x 以外的版本 |
| Rust/Cargo | 仅启用 truck 后端时需要 | 构建实验性的 `truck-bridge` |

项目固定使用 C++23 和动态 MSVC 运行库：Release 等配置使用 `/MD`，Debug 使用 `/MDd`。Qt 由 vcpkg manifest 提供，并由项目精确要求为 Qt 6.11.1。

### 1.2 拉取子模块

新克隆仓库时使用：

```powershell
git clone --recurse-submodules <repository-url>
cd mulan
```

已有工作区可补齐子模块：

```powershell
git submodule update --init --recursive
```

`msdf-atlas-gen` 是常规构建所需子模块；`truck-bridge` 只在 `MULAN_ENABLE_TRUCK_BACKEND=ON` 时参与构建。

### 1.3 准备 vcpkg 与 OCCT

当前 Preset 从父进程环境读取三个路径：

```powershell
$env:VCPKG_ROOT = '<vcpkg-root>'
$env:OCCT_ROOT = '<occt-7.9.x-root>'
$env:OCCT_3RDPARTY_DIR = '<occt-third-party-root>'
```

请替换为本机真实路径。`VCPKG_ROOT` 必须在运行 `cmake --preset ...` 前存在于当前进程环境中，因为 `CMakePresets.json` 通过 `$penv{VCPKG_ROOT}` 构造工具链路径。

`OCCT_ROOT` 必须至少具有以下结构：

```text
<OCCT_ROOT>/
├─ inc/Standard_Version.hxx
└─ win64/vc14/
   ├─ lib/*.lib
   └─ bin/*.dll
```

`OCCT_3RDPARTY_DIR` 可以直接指向第三方运行库根目录。配置逻辑还会在其自身、`bin`、一级子目录的 `bin`/`redist` 及其 `win64`/`x64` 子目录中搜索 DLL。若不使用 OCCT 后端，可以不设置两个 OCCT 路径，并在配置时传入 `-DMULAN_ENABLE_OCCT_BACKEND=OFF`。

首次配置会在构建目录中创建 `vcpkg_installed` 并安装 `vcpkg.json` 中锁定的依赖，通常耗时较长；下面的二进制缓存用于避免之后反复编译同一批依赖。

### 1.4 必须启用 vcpkg 二进制缓存

项目依赖 Qt、Vulkan、Assimp、GTest、Slang 等较大的 vcpkg 包。建议把二进制缓存视为本地构建环境的必需配置；否则新建 `build` 目录、切换普通/Tracy 构建或清理安装树后，vcpkg 可能再次从源码编译依赖。

先创建一个位于源码和构建目录之外的持久缓存目录，再设置 `VCPKG_BINARY_SOURCES`：

```powershell
$vcpkgBinaryCache = Join-Path $env:LOCALAPPDATA 'Mulan\vcpkg-binary-cache'
New-Item -ItemType Directory -Force -Path $vcpkgBinaryCache | Out-Null
$env:VCPKG_BINARY_SOURCES = "clear;files,$($vcpkgBinaryCache.Replace('\', '/')),readwrite"
```

然后再运行配置：

```powershell
cmake --preset msvc
```

`readwrite` 表示配置时优先读取已有二进制包，并把本次生成的新包写回缓存。缓存目录不要放进 `build/msvc` 或 `build/msvc-tracy`：两个构建树应共享同一个持久缓存，删除构建目录时也不应删除它。

如需让新开的 PowerShell 也自动使用缓存，可设置用户级环境变量，然后重新打开终端：

```powershell
[Environment]::SetEnvironmentVariable(
    'VCPKG_BINARY_SOURCES',
    "clear;files,$($vcpkgBinaryCache.Replace('\', '/')),readwrite",
    'User'
)
```

CI 采用相同思路：`VCPKG_BINARY_SOURCES` 指向工作区 `.vcpkg-cache`，再由 GitHub Actions cache 在不同任务之间保存和恢复。缓存 key 包含 `vcpkg.json`、可能存在的 `vcpkg-configuration.json` 以及 `cmake/vcpkg-overlay-ports/**` 的哈希，依赖声明改变后会生成新的缓存版本。

### 1.5 OCCT 明确不由 vcpkg 构建

OpenCASCADE 当前不集成进 vcpkg。它使用官方预编译的 7.9.x Windows vc14 64 位包，通过 `OCCT_ROOT` 和 `OCCT_3RDPARTY_DIR` 直接链接和部署；这样可以避免在已经较慢的 vcpkg 依赖安装之外再从源码构建 OCCT。

这意味着 vcpkg 二进制缓存只负责 `vcpkg.json` 中的依赖，不会缓存或安装 OCCT。团队或 CI 应单独保存 OCCT 预编译包；现有 CI 使用 `opencascade-7.9.3-vc14-64-combined.zip`，并通过独立的 GitHub Actions cache 恢复到 Runner 的专用 OCCT 目录。

## 2. 推荐构建方式：CMake Presets

查看当前所有 Preset：

```powershell
cmake --list-presets=all
```

### 2.1 Preset 一览

| 类型 | 名称 | 构建目录/配置 | 说明 |
| --- | --- | --- | --- |
| configure | `msvc` | `build/msvc` | MSVC x64 日常配置；内置 profiler |
| configure | `msvc-tracy` | `build/msvc-tracy` | 启用 Tracy vcpkg feature 和 Tracy profiler |
| build | `release` | `build/msvc`, Release | 发布优化构建 |
| build | `relwithdebinfo` | `build/msvc`, RelWithDebInfo | 带调试信息的优化构建；日常性能分析配置 |
| build | `relwithdebinfo-tracy` | `build/msvc-tracy`, RelWithDebInfo | 临时 Tracy 诊断构建 |

当前没有 Debug build Preset，但 Visual Studio 是多配置生成器，可以直接指定 `--config Debug`。

### 2.2 日常 Release 构建

```powershell
cmake --preset msvc
cmake --build --preset release --parallel
```

### 2.3 RelWithDebInfo 构建

```powershell
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
```

`builtin` 和 `tracy` profiler 的埋点只在 RelWithDebInfo 中启用；Release、Debug 和 MinSizeRel 中会编译为关闭状态。

### 2.4 Debug 构建

```powershell
cmake --preset msvc
cmake --build build/msvc --config Debug --parallel
```

### 2.5 Tracy 临时诊断构建

```powershell
cmake --preset msvc-tracy
cmake --build --preset relwithdebinfo-tracy --parallel
```

Tracy 使用独立的 `build/msvc-tracy`，不会污染普通构建缓存。该配置通过 `VCPKG_MANIFEST_FEATURES=tracy-profiler` 安装 Tracy，并要求 `MULAN_PROFILER_BACKEND=tracy`。

### 2.6 覆盖 Preset 变量

可以在 Preset 后追加 `-D` 参数。例如只构建 D3D12、关闭测试和 OCCT：

```powershell
cmake --preset msvc -DMULAN_ENABLE_VULKAN_BACKEND=OFF -DMULAN_ENABLE_D3D11_BACKEND=OFF -DMULAN_ENABLE_OPENGL_BACKEND=OFF -DMULAN_ENABLE_OCCT_BACKEND=OFF -DBUILD_TESTING=OFF
cmake --build --preset release --parallel
```

修改变量后重新运行配置即可。不要在 `build/msvc` 和 `build/msvc-tracy` 之间复用缓存。

## 3. 不使用 Preset 的等价配置

Preset 是推荐且由 CI 验证的入口。需要自定义构建目录时，可显式传入相同参数：

```powershell
cmake -S . -B build/custom -G 'Visual Studio 18 2026' -A x64 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_OVERLAY_PORTS="$PWD/cmake/vcpkg-overlay-ports" -DOCCT_ROOT="$env:OCCT_ROOT" -DOCCT_3RDPARTY_DIR="$env:OCCT_3RDPARTY_DIR"
cmake --build build/custom --config RelWithDebInfo --parallel
```

若选择其他生成器，需要自行保证其编译器支持 C++23、Windows 平台依赖可用，并为单配置生成器设置 `CMAKE_BUILD_TYPE`。当前仓库的 Preset、VS Code 任务和 CI 均以 Visual Studio 多配置生成器为准。

## 4. 构建目标、产物与运行

### 4.1 常用目标

| 目标 | 作用 |
| --- | --- |
| `mulan` | 构建主程序；同时构建启用的 RHI、建模插件和 shader |
| `mulan_shaders` | 根据启用的 RHI 后端生成 SPIR-V、DXBC、DXIL 和/或 OpenGL SPIR-V |
| `mulan_dev_runtime` | 构建主程序并准备最小开发运行环境：OCCT/第三方 DLL 和必需 Qt 插件 |
| `mulan_deploy_runtime` | 准备独立运行所需的链接 DLL、OCCT DLL、建模插件和 Qt 运行时；仅在 `MULAN_ENABLE_RUNTIME_DEPLOY_TARGET=ON` 时存在 |
| `truck_bridge_build` | 用 Cargo 构建 truck bridge；仅在 truck 后端启用时存在 |

仅构建指定目标的示例：

```powershell
cmake --build build/msvc --config RelWithDebInfo --target mulan_shaders --parallel
cmake --build build/msvc --config RelWithDebInfo --target mulan_dev_runtime --parallel
cmake --build build/msvc --config Release --target mulan_deploy_runtime --parallel
```

### 4.2 输出目录

以 `build/msvc` 为例：

| 内容 | 路径 |
| --- | --- |
| 主程序和 DLL | `build/msvc/bin/<Config>/` |
| 主程序 | `build/msvc/bin/<Config>/mulan.exe` |
| 建模插件 | 主程序目录下的 `backends/` |
| 编译后的 shader | `build/msvc/shaders/` |
| 复制后的公共头 | `build/msvc/include/mulan/` |
| vcpkg 安装树 | `build/msvc/vcpkg_installed/x64-windows/` |
| 编译数据库 | `build/msvc/compile_commands.json`（生成器支持时） |

`<Config>` 为 `Debug`、`Release`、`RelWithDebInfo` 或 `MinSizeRel`。

### 4.3 开发运行与部署

推荐的开发运行流程：

```powershell
cmake --build build/msvc --config RelWithDebInfo --target mulan_dev_runtime --parallel
& .\build\msvc\bin\RelWithDebInfo\mulan.exe
```

`MULAN_ENABLE_DEV_RUNTIME_ENV=ON` 时，生成的 Visual Studio 项目还会为 F5 设置 `PATH`、Qt 插件路径和工作目录，不要求把所有运行库复制到输出目录。

需要从输出目录独立运行或打包前，执行：

```powershell
cmake --build build/msvc --config Release --target mulan_deploy_runtime --parallel
```

显式部署目标默认运行 `windeployqt`。日常开发目标刻意不运行它，以缩短迭代时间。若希望每次构建 `mulan` 后都执行部署，可设置 `MULAN_ENABLE_POST_BUILD_DEPLOY=ON`；这通常不适合作为日常默认值。

### 4.4 VS Code

仓库已提供 `.vscode/tasks.json` 和 `.vscode/launch.json`：

- 默认构建任务：`CMake: Build (RelWithDebInfo)`；
- Release：`CMake: Build (Release)`；
- 最小开发运行时：`CMake: Prepare Runtime (RelWithDebInfo)`；
- 完整部署：`CMake: Deploy (RelWithDebInfo)`；
- 调试配置：`Debug mulan (RelWithDebInfo)`，启动前自动执行开发运行时目标。

这些任务均假定配置目录为 `build/msvc`。

## 5. 测试

`include(CTest)` 使 `BUILD_TESTING` 默认值为 `ON`。完成构建后运行：

```powershell
ctest --test-dir build/msvc -C Release --output-on-failure
ctest --test-dir build/msvc -C RelWithDebInfo --output-on-failure
```

列出测试但不执行：

```powershell
ctest --test-dir build/msvc -C Release -N
```

CI 在无 Vulkan ICD 和 OpenGL 图形上下文的 Windows Runner 上使用以下过滤器，后端仍会参与编译：

```powershell
$env:GTEST_FILTER = '-*Vulkan*:*OpenGL*'
ctest --test-dir build/msvc -C Release --output-on-failure
```

若只需要构建应用、希望缩短配置和构建时间，可在首次配置时传入 `-DBUILD_TESTING=OFF`。

## 6. CMake 配置变量完整清单

本节的“完整”指仓库自身定义、允许使用者通过缓存或命令行配置的变量；CMake、生成器和 vcpkg 自动生成的数百个内部缓存项不属于项目 API。变量名大小写敏感，BOOL 值建议使用 `ON`/`OFF`，列表型 STRING 使用分号分隔。

### 6.1 功能与后端

| 变量 | 类型 | 默认值 | 说明与约束 |
| --- | --- | --- | --- |
| `MULAN_ENABLE_OCCT_BACKEND` | BOOL | `ON` | 构建 OpenCASCADE 建模插件。开启时必须提供有效的 OCCT 7.9.x。 |
| `MULAN_ENABLE_TRUCK_BACKEND` | BOOL | `OFF` | 构建实验性 truck 插件和 Rust bridge。开启时需要 Cargo 及完整 `truck-bridge` 子模块。 |
| `MULAN_DEFAULT_SHAPE_OPS_BACKEND` | STRING | `occt` | 运行时未覆盖时默认使用的 shape-ops 后端；只接受 `occt` 或 `truck`，不区分大小写。若所选后端未构建，会回退到另一个已启用后端；两者都关闭时为 `none`。 |
| `MULAN_ENABLE_VULKAN_BACKEND` | BOOL | `ON` | 构建 Vulkan RHI，并生成 Vulkan SPIR-V；需要 Vulkan 和 Vulkan Memory Allocator。 |
| `MULAN_ENABLE_D3D12_BACKEND` | BOOL | Windows 上 `ON` | 构建 D3D12 RHI，并生成 DXIL；实际只在 Windows 添加目标。 |
| `MULAN_ENABLE_D3D11_BACKEND` | BOOL | Windows 上 `ON` | 构建 D3D11 RHI，并生成 DXBC；实际只在 Windows 添加目标。 |
| `MULAN_ENABLE_OPENGL_BACKEND` | BOOL | Windows 上 `ON` | 构建原生 OpenGL RHI，并通过 Slang + glslangValidator 生成 OpenGL SPIR-V。 |
| `MULAN_SHADER_DEBUG_INFO` | BOOL | `ON` | 为编译后的 shader 添加符号和源码行映射信息，便于 RenderDoc/PIX 调试。它是配置期统一开关。 |
| `MULAN_PROFILER_BACKEND` | STRING | `builtin` | 只接受 `builtin`、`tracy`、`none`。`builtin`/`tracy` 仅在 RelWithDebInfo 启用埋点；`none` 完全移除埋点。Tracy 还需要 vcpkg 的 `tracy-profiler` feature。 |

### 6.2 运行时与部署

| 变量 | 类型 | 默认值 | 说明与约束 |
| --- | --- | --- | --- |
| `MULAN_ENABLE_DEV_RUNTIME_ENV` | BOOL | `ON` | 为 Visual Studio F5 配置运行库搜索路径、Qt 插件路径和工作目录。不会自行复制全部 DLL。 |
| `MULAN_ENABLE_POST_BUILD_DEPLOY` | BOOL | `OFF` | 每次构建主程序后复制 OCCT/第三方运行库；若 Qt 部署开关也开启，则运行 `windeployqt`。 |
| `MULAN_ENABLE_RUNTIME_DEPLOY_TARGET` | BOOL | `ON` | 创建显式的 `mulan_deploy_runtime` 及其辅助目标。关闭后这些部署目标不存在。 |
| `MULAN_DEPLOY_QT_WITH_WINDEPLOYQT` | BOOL | `ON` | 在显式部署流程中使用 `windeployqt`；在 post-build 部署开启时也控制 post-build 的 Qt 部署。找不到该工具时不会创建对应步骤。 |
| `OCCT_RUNTIME_USER_EXTRA_DLLS` | STRING 列表 | 空 | 在 OCCT 自身 `win64/vc14/bin` 中额外查找并复制的 DLL 名称，例如 `foo.dll;bar.dll`。缺失项产生警告。 |
| `MULAN_OCCT_EXTRA_3RDPARTY_DLL_NAMES` | STRING 列表 | 空 | 在 `OCCT_3RDPARTY_DIR` 搜索路径中额外查找并复制的第三方 DLL 名称。缺失项产生警告。 |

项目始终额外检查一组 OCCT 运行 DLL，以及 `FreeImage.dll`、`freetype.dll`、`tbb12.dll` 等既定第三方 DLL；这些必需名称由源码维护，不是用户配置变量。

### 6.3 依赖与工具路径

| 变量 | 类型 | 默认/来源 | 说明与查找顺序 |
| --- | --- | --- | --- |
| `OCCT_ROOT` | PATH | 无；`msvc` Preset 读取同名环境变量 | OCCT 根目录。开启 OCCT 后端时必需。命令行/Preset 缓存值有效时优先，否则尝试同名环境变量。必须存在并满足 7.9.x 目录布局。 |
| `OCCT_3RDPARTY_DIR` | PATH | 无；`msvc` Preset 读取同名环境变量 | OCCT 第三方运行库根目录。缓存值有效时优先，否则尝试同名环境变量。路径缺失会警告，之后运行时 DLL 也可能缺失。 |
| `MULAN_TRUCK_BRIDGE_SOURCE_DIR` | PATH | `3rdparty/truck-bridge` | truck bridge 源码位置；开启 truck 后端时必须包含 `Cargo.toml`、`Cargo.lock`、`build.rs` 和 `cbindgen.toml`。 |
| `MULAN_SLANGC_EXECUTABLE` | FILEPATH | 自动发现 | Slang 编译器绝对路径。查找顺序为显式缓存值、vcpkg 工具目录、`VULKAN_SDK` 的 `Bin`/`Bin32`、系统可执行程序搜索路径。只要任一 shader 输出格式启用就必须可用。 |
| `MULAN_GLSLANG_VALIDATOR_EXECUTABLE` | FILEPATH | 自动发现 | `glslangValidator` 绝对路径；仅启用 OpenGL 后端时需要，查找来源同上。 |

配置成功后还会出现 `OCCT_LIBRARY_<模块名>` 缓存项。它们由 `find_library` 根据 `OCCT_ROOT` 自动生成，不应手工逐项维护；更换 OCCT 安装位置时应重新配置，必要时使用新的构建目录。

### 6.4 标准 CMake 与 vcpkg 变量

这些变量并非 Mulan 自定义，但当前构建入口会直接使用：

| 变量 | 当前值/默认值 | 说明 |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | CTest 标准开关；控制是否添加 `tests/`。 |
| `CMAKE_TOOLCHAIN_FILE` | `${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` | Preset 设置的 vcpkg 工具链。应在首次配置时确定，之后不要在同一缓存中切换。 |
| `VCPKG_OVERLAY_PORTS` | `cmake/vcpkg-overlay-ports` | 使用仓库内 overlay port；当前包含 Tracy 的项目覆盖。 |
| `VCPKG_MANIFEST_FEATURES` | 普通构建为空；Tracy 为 `tracy-profiler` | 选择 `vcpkg.json` 的可选 feature。 |
| `VCPKG_TARGET_TRIPLET` | 通常为 `x64-windows` | 由 x64 架构和 vcpkg 工具链确定；改变 triplet 前应确认 Qt/运行库策略兼容。 |
| `CMAKE_BUILD_TYPE` | Visual Studio 下不使用 | 只对单配置生成器有效；Visual Studio 使用构建命令的 `--config`。 |

以下值由顶层项目固定，不作为受支持的外部开关：

- `CMAKE_CXX_STANDARD=23`、`CMAKE_CXX_STANDARD_REQUIRED=ON`；
- `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`；
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`；
- `CMAKE_RUNTIME_OUTPUT_DIRECTORY=<binary>/bin`、`CMAKE_LIBRARY_OUTPUT_DIRECTORY=<binary>/bin`；
- `MULAN_SHADER_OUTPUT_DIR=<binary>/shaders`；
- 应用子目录中的 `CMAKE_AUTOMOC=ON`、`CMAKE_AUTORCC=ON`。

内嵌的 `msdf-atlas-gen` 还被项目强制配置为：

| 变量 | 强制值 |
| --- | --- |
| `MSDF_ATLAS_MSDFGEN_EXTERNAL` | `OFF` |
| `MSDF_ATLAS_BUILD_STANDALONE` | `OFF` |
| `MSDF_ATLAS_USE_VCPKG` | `OFF` |
| `MSDF_ATLAS_USE_SKIA` | `OFF` |
| `MSDF_ATLAS_NO_ARTERY_FONT` | `ON` |
| `MSDF_ATLAS_DYNAMIC_RUNTIME` | `ON` |

这些第三方变量以 `CACHE ... FORCE` 写入，命令行覆盖不会生效；列在这里是为了完整说明当前构建图，而不是鼓励修改它们。

## 7. 构建相关环境变量

| 环境变量 | 是否必需 | 用途 |
| --- | --- | --- |
| `VCPKG_ROOT` | 使用仓库 Preset 时必需 | 定位 vcpkg CMake 工具链。 |
| `OCCT_ROOT` | 默认 OCCT 后端开启时必需，除非用 `-DOCCT_ROOT=...` | 作为 OCCT 根目录的回退来源，也是 `msvc` Preset 的输入。 |
| `OCCT_3RDPARTY_DIR` | 建议设置，除非用 `-DOCCT_3RDPARTY_DIR=...` | 定位 OCCT 第三方运行 DLL。 |
| `VULKAN_SDK` | 可选 | vcpkg 工具未找到时，为 `slangc` 和 `glslangValidator` 提供回退搜索目录。Vulkan 库本身由 vcpkg 提供。 |
| `VCPKG_BINARY_SOURCES` | 可选；CI 使用 | 配置 vcpkg 二进制缓存。CI 指向工作区 `.vcpkg-cache`。 |
| `GTEST_FILTER` | 可选 | 运行测试时筛选 GoogleTest 用例；CI 用它跳过需要 Vulkan/OpenGL 运行环境的用例。 |

运行时还可能使用后端选择相关环境覆盖，但它们不参与 CMake 配置，因此不属于本文的构建变量。

## 8. 常见问题

### 找不到 OCCT 或版本不受支持

确认 `OCCT_ROOT` 指向直接包含 `inc` 和 `win64/vc14` 的目录，而不是它的父目录；项目只接受 7.9.x。修改路径后重新运行 `cmake --preset msvc`。

### 找不到 `slangc` / `glslangValidator`

先确认 vcpkg manifest 安装成功，工具通常位于：

```text
build/msvc/vcpkg_installed/x64-windows/tools/shader-slang/slangc.exe
build/msvc/vcpkg_installed/x64-windows/tools/glslang/glslangValidator.exe
```

也可以显式传入 `-DMULAN_SLANGC_EXECUTABLE=...` 和 `-DMULAN_GLSLANG_VALIDATOR_EXECUTABLE=...`。后者只在 OpenGL 后端开启时需要。

### 程序启动时缺少 DLL 或 Qt platform plugin

开发时先构建 `mulan_dev_runtime`；需要可独立运行的目录时构建 `mulan_deploy_runtime`。同时确认 `OCCT_3RDPARTY_DIR` 指向正确的 combined-package 第三方目录。

### 修改变量后行为没有变化

先检查缓存中的实际值：

```powershell
cmake -N -LA build/msvc
```

Tracy 和普通构建使用不同目录。如果工具链、架构或依赖根目录发生较大变化，优先使用新的构建目录重新配置，避免旧缓存中的自动发现路径残留。
