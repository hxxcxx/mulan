//! # truck-bridge
//!
//! [truck] 几何/CAD 内核的 C ABI 导出层。
//!
//! 此 crate 通过精简、稳定的 C ABI 暴露 truck 的类型。它会生成动态库
//! (`truck_bridge.dll` / `.so` / `.dylib`) 和静态库，并在
//! `include/truck_bridge.h` 生成 C 头文件。
//!
//! ## 设计不变量（“四项基础”）
//!
//! 以下约定在此统一确立，并由所有导出类型复用：
//!
//! 1. **错误模型** — 参见 [`error`]。可能失败的函数返回 `bool`（或可为 NULL
//!    的句柄），并可选择通过 `TruckError` 句柄报告详细信息。panic 绝不会跨越
//!    FFI 边界：每个 `extern "C"` 函数体都在 `catch_unwind` 保护下运行。
//! 2. **所有权** — 参见 [`handle`]。Truck 对象保存在*不透明句柄*之后（C
//!    永远无法看到其内存布局）。返回的数组/字符串采用 `{ptr, len}` 视图，
//!    每种视图都有与之匹配的 `*_free`。
//! 3. **ABI 版本** — 参见 [`version`]。使用方可通过 `truck_abi_version()`
//!    在加载时拒绝 ABI 不匹配的 `.dll`。
//! 4. **生成的头文件** — `build.rs` 通过 cbindgen 重新生成
//!    `truck_bridge.h`，切勿手动编辑该文件。
//!
//! [truck]: https://github.com/ricosjp/truck
//!
//! ## 不在本项目范围内
//!
//! 此处有意不提供 C++（或其他语言）的 RAII 包装器。使用方应基于 C 头文件
//! 自行构建轻量包装层，使其偏好的惯用法（智能指针、异常/optional、标准库
//! 选择）和编译器不受本库限制。

#![deny(unsafe_op_in_unsafe_fn)]
// 适度放宽少数与 FFI 冲突的规则，避免 clippy 产生无意义的告警。
#![allow(clippy::missing_safety_doc)]
// 公共 FFI 项的 `missing_docs` 在模块级别强制执行。

pub mod error;
pub mod handle;
pub mod polymesh;
pub mod topology;
pub mod version;

// 重新导出 polymesh 使用的具体 truck 类型，确保整个 crate 统一使用
// `PolygonMesh<V, A>` 的同一种单态化形式。
pub(crate) use truck_polymesh::PolygonMesh;
