# Mulan 构建指南

Mulan 支持 Windows x64 和 Linux x64。项目使用 C++23、CMake Presets 和 vcpkg manifest；
下列命令均在仓库根目录执行。

| 平台 | 日常构建 | 图形后端 | OCCT |
| --- | --- | --- | --- |
| Windows | Visual Studio，Debug/RelWithDebInfo/Release | Vulkan、D3D12、D3D11、OpenGL | 官方预编译包 7.9.x |
| Linux | Ninja，RelWithDebInfo | Vulkan、OpenGL（X11/XCB） | 系统开发包 7.6.x |

## 1. 公共准备

必需工具：Git、CMake 3.24+、支持 C++23 的编译器和 vcpkg。

首次拉取代码时必须包含子模块：

```bash
git clone --recurse-submodules <repository-url>
cd mulan
```

已有工作区可执行：

```bash
git submodule update --init --recursive
```

准备 vcpkg（路径可自定义）：

```bash
git clone https://github.com/microsoft/vcpkg.git <vcpkg-root>
```

Windows 执行 `bootstrap-vcpkg.bat`，Linux 执行 `bootstrap-vcpkg.sh -disableMetrics`。
配置前需要指定 vcpkg toolchain：

```powershell
# Windows PowerShell
$env:VCPKG_ROOT = '<vcpkg-root>'
$env:CMAKE_TOOLCHAIN_FILE = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

```bash
# Linux
export VCPKG_ROOT="$HOME/tools/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Windows preset 直接读取父进程的 `CMAKE_TOOLCHAIN_FILE`。Visual Studio 开发
环境可能把 `VCPKG_ROOT` 改为其内置 vcpkg，因此不能用它推导 Windows toolchain。
Linux preset 仍通过 `VCPKG_ROOT` 构造路径。首次配置会由 vcpkg 自动安装依赖。

### vcpkg 二进制缓存

项目包含 Qt、Slang 和 Assimp 等较大依赖，建议启用持久二进制缓存，避免创建新
build 目录或切换 Tracy 构建时重新编译全部依赖。

```powershell
# Windows PowerShell
$vcpkgCache = Join-Path $env:LOCALAPPDATA 'Mulan\vcpkg-cache'
New-Item -ItemType Directory -Force -Path $vcpkgCache | Out-Null
$env:VCPKG_BINARY_SOURCES = "clear;files,$($vcpkgCache.Replace('\', '/')),readwrite"
```

```bash
# Linux
mkdir -p "$HOME/.cache/vcpkg/archives"
export VCPKG_BINARY_SOURCES="clear;files,$HOME/.cache/vcpkg/archives,readwrite"
```

缓存目录应位于源码和 `build/` 之外，并在运行 configure preset 之前设置。
`readwrite` 表示优先恢复已缓存的包，同时把新构建的包写回缓存。

## 2. Windows 构建

### 2.1 环境

- Visual Studio，需提供 `Visual Studio 18 2026` 生成器和 x64 C++ 工具链。
- OCCT 7.9.x 官方 Windows 预编译包。
- OCCT 配套的第三方运行库。

```powershell
$env:OCCT_ROOT = '<occt-7.9.x-root>'
$env:OCCT_3RDPARTY_DIR = '<occt-third-party-root>'
```

`OCCT_ROOT` 中必须存在 `cmake/OpenCASCADEConfig.cmake`。项目从 OCCT 官方
CMake package 读取头文件、库和 DLL 路径，不再在项目内硬编码 `vc14` 库目录。

### 2.2 配置与构建

日常开发使用 RelWithDebInfo：

```powershell
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
```

发布优化构建：

```powershell
cmake --build --preset release --parallel
```

需要完整观察局部变量或逐行调试时，使用 Debug：

```powershell
cmake --build --preset debug --parallel
& .\build\msvc\bin\Debug\mulan.exe
```

Debug、RelWithDebInfo 和 Release 共用 `build/msvc` 构建树，并分别输出到 `bin` 下的
同名目录。性能测试应使用 RelWithDebInfo 或 Release。

Windows preset 会在主程序链接后以 `copy_if_different` 补齐开发运行所需的 OCCT
依赖和 Qt 插件，构建完成后可以直接运行：

```powershell
& .\build\msvc\bin\RelWithDebInfo\mulan.exe
```

仅在制作独立运行目录时执行完整部署：

```powershell
cmake --build build/msvc --config Release --target mulan_deploy_runtime --parallel
```

### 2.3 测试

```powershell
ctest --test-dir build/msvc -C RelWithDebInfo --output-on-failure
```

## 3. Linux 构建

当前验证环境为 Ubuntu 24.04（包括 WSL2）、GCC 13 和 Ninja。Linux 使用
X11/XCB，不依赖 Wayland 平台插件。

### 3.1 系统依赖

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build git curl zip unzip pkg-config \
  autoconf autoconf-archive automake libtool bison flex python3 \
  libgl1-mesa-dev libx11-dev libx11-xcb-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev \
  libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
  libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev libxcb-util-dev \
  libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-xinput-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev \
  libocct-foundation-dev libocct-modeling-data-dev \
  libocct-modeling-algorithms-dev libocct-data-exchange-dev libtbb-dev
```

Ubuntu 24.04 上这些 OCCT 包提供 7.6.3。`libtbb-dev` 必须显式安装：OCCT 的
CMake targets 会引用 TBB 链接文件，仅安装 TBB runtime 不足以完成构建。

### 3.2 配置、构建与测试

```bash
cmake --preset linux
cmake --build --preset linux-relwithdebinfo --parallel
ctest --preset linux-relwithdebinfo
```

Linux preset 默认：

- 构建 RelWithDebInfo；
- 启用 Vulkan、OpenGL 和 OCCT；
- 关闭 D3D11/D3D12、truck 和 Windows 部署目标；
- 使用 `x64-linux` vcpkg triplet 和 `linux-desktop` manifest feature。

运行：

```bash
./build/linux/bin/mulan
```

WSL 中需要 WSLg 或可用的 X Server。`echo "$DISPLAY"` 为空、窗口无法显示属于
图形会话环境问题，不影响 Linux 编译和无界面测试。

## 4. 产物位置

| 内容 | Windows | Linux |
| --- | --- | --- |
| 主程序 | `build/msvc/bin/<Config>/mulan.exe` | `build/linux/bin/mulan` |
| 建模插件 | `build/msvc/bin/<Config>/backends/` | `build/linux/bin/backends/` |
| Shader | `build/msvc/shaders/` | `build/linux/shaders/` |
| 生成的公共头 | `build/msvc/include/mulan/` | `build/linux/include/mulan/` |
| vcpkg 安装树 | `build/msvc/vcpkg_installed/` | `build/linux/vcpkg_installed/` |

Linux OCCT 插件为 `bin/backends/libmulan_modeling_occt.so`，并使用 `$ORIGIN/..`
RPATH 定位 `bin/` 中的 Mulan 共享库。Windows 插件为
`backends/mulan_modeling_occt.dll`。

## 5. 少量常用定制

关闭测试或某个后端时，在 configure preset 后追加 `-D`：

```bash
cmake --preset linux -DBUILD_TESTING=OFF -DMULAN_ENABLE_OPENGL_BACKEND=OFF
```

Windows 临时使用 Tracy：

```powershell
cmake --preset msvc-tracy
cmake --build --preset relwithdebinfo-tracy --parallel
```

除非正在排查时间线或锁竞争，不必使用 Tracy 构建。日常使用 `builtin`
profiler 的 RelWithDebInfo 即可。

## 6. 常见问题

### Preset 报 vcpkg toolchain 不存在

Windows 确认 `CMAKE_TOOLCHAIN_FILE` 指向目标 vcpkg 的
`scripts/buildsystems/vcpkg.cmake`；Linux 确认 `VCPKG_ROOT` 已设置。修改环境变量
后需在同一终端重新执行 configure preset。

### Windows 找不到 OCCT

确认 `OCCT_ROOT/cmake/OpenCASCADEConfig.cmake` 存在，版本为 7.9.x；
`OCCT_3RDPARTY_DIR` 应指向对应的第三方运行库根目录。

### Linux 找不到 OCCT 或 TBB

确认已安装上述四个 `libocct-*-dev` 包和 `libtbb-dev`。项目目前明确接受
Linux OCCT 7.6.x，不会使用 Windows 的 OCCT 预编译包。

### Windows 运行时缺少 DLL 或 Qt platform plugin

重新执行 Windows configure preset，并正常构建 `mulan`。Windows preset 会自动准备
最小开发运行时；需要完整可分发目录时再构建 `mulan_deploy_runtime`。

### 大幅修改工具链后缓存异常

工具链、架构或依赖根目录发生改变时，使用新的 build 目录或删除对应目录后重新
configure。不要在 Windows 和 Linux 之间复用同一个 CMake cache。
