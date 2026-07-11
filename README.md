# Mulan

Mulan 是一个面向 Windows 的 C++23 CAD 与实时渲染项目，使用 Qt 6、OpenCASCADE、Vulkan 和 Direct3D 12。

## 构建环境

- Windows 10 或更高版本
- Visual Studio 2026，包含“使用 C++ 的桌面开发”工作负载
- CMake 3.24 或更高版本
- Qt 6.8 或更高的 6.x 版本，使用官方 MSVC x64 包构建
- OpenCASCADE 7.9.x x64 combined package
- vcpkg，仓库通过 `vcpkg.json` 固定 baseline

配置前需要设置以下环境变量：

```powershell
$env:VCPKG_ROOT = "F:/APP/vcpkg"
$env:QTDIR = "F:/dev/qt6/6.8.3/msvc2022_64" # Qt 官方包名，兼容 VS 2026
$env:OCCT_ROOT = "F:/dev/opencascade-7.9.3-vc14-64"
$env:OCCT_3RDPARTY_DIR = "F:/dev/3rdparty-vc14-64"
```

也可以使用 `Qt6_DIR` 代替 `QTDIR`。`OCCT_ROOT` 应包含 `inc` 和 `win64/vc14`，`OCCT_3RDPARTY_DIR` 应指向 OCCT combined package 中的第三方运行库根目录。

## 配置与构建

```powershell
cmake --preset msvc
cmake --build --preset debug --parallel
```

其他构建配置：

```powershell
cmake --build --preset release --parallel
cmake --build --preset relwithdebinfo --parallel
```

生成目录固定为 `build/msvc`。不要在同一生成目录中混用其他 generator 或架构。

## 运行与部署

Visual Studio F5 调试使用 CMake 配置的运行时搜索路径，普通开发构建默认不会反复复制 Qt 和 OCCT 运行库。

需要生成可独立运行的目录时执行：

```powershell
cmake --build build/msvc --config Debug --target mulan_deploy_runtime --parallel
```

应用和部署后的依赖位于 `build/msvc/bin/<配置>`，建模插件位于其 `backends` 子目录。

## 可选后端

以下选项可以在配置时覆盖：

- `MULAN_ENABLE_VULKAN_BACKEND`：构建 Vulkan RHI，默认开启。
- `MULAN_ENABLE_D3D12_BACKEND`：构建 Direct3D 12 RHI，Windows 默认开启。
- `MULAN_ENABLE_OCCT_BACKEND`：构建 OpenCASCADE 建模插件，默认开启。
- `MULAN_ENABLE_TRUCK_BACKEND`：构建实验性 truck 建模插件，默认关闭。

例如构建不依赖 OCCT 的版本：

```powershell
cmake -S . -B build/no-occt -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DMULAN_ENABLE_OCCT_BACKEND=OFF
```

若 Vulkan 与 Direct3D 12 都关闭，构建不再要求 `dxc`，也不会生成 shader 产物。

## 第三方组件

- [msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen)：以 Git submodule 引入。
- [SARibbon](https://github.com/czyt1988/SARibbon)：2.5.7 amalgamated source，MIT License。
- `truck-bridge`：仓库内实验性 Rust/C++ bridge，许可证见其目录。
- Vulkan headers、loader、VMA、DXC 和其余 C++ 包由 vcpkg manifest 安装，具体版本由 `vcpkg.json` 的 baseline 决定。

项目许可证见 [LICENSE](LICENSE)。第三方组件保留各自许可证。
