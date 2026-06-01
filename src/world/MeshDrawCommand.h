#pragma once

// MeshDrawCommand 已迁移到 Engine 层
// 此文件通过 build/include/ 路径间接包含 Engine 头文件
// 使用 world 命名空间别名保持兼容性

// 通过绝对路径包含 Engine 的 MeshDrawCommand（避免双路径重定义）
// Engine 头文件在 build/include/mulan/engine/render/ 下
// 但它们使用相对路径，所以我们在这里手动定义别名

namespace mulan::engine {
struct MeshDrawCommand;
}

namespace mulan::world {
    using MeshDrawCommand = engine::MeshDrawCommand;
}

