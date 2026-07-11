# Engine - 渲染引擎容器目录，包含 4 个独立 target：
#   rhi          硬件抽象层（内部模块）
#   render       渲染管线（对外暴露）
#   platform     平台/窗口层（对外暴露）
#   interaction  交互层（对外暴露）
add_subdirectory(rhi)
add_subdirectory(render)
add_subdirectory(platform)
add_subdirectory(interaction)
