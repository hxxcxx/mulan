# truck-bridge

[truck] Rust 几何/CAD 内核的 **C ABI** 导出层，让 truck 可以被 C、C++ 或任何支持 C
接口的语言调用。

> truck-bridge **仅提供 C ABI**（`truck_bridge.dll` / `.so` / `.dylib`、
> `truck_bridge.lib`，以及自动生成的 `include/truck_bridge.h`）。这里**刻意不提供**
> C++（或其他语言）的 RAII 封装 — 每个消费者自行构建薄封装层，以便使用自己习惯的
> 编程风格、智能指针类型和编译器，不受本库约束。

[truck]: https://github.com/ricosjp/truck

## 构建

```bash
cargo build --release
```

在 `target/release/` 下生成：

| 产物 | 说明 |
|---|---|
| `truck_bridge.dll`（`.so`/`.dylib`） | 动态库 |
| `truck_bridge.dll.lib` / 导入库 | 用于链接 dll |
| `truck_bridge.lib` | 静态库 |

每次构建时 `build.rs` 会通过 [cbindgen] 重新生成 `include/truck_bridge.h`。
头文件已提交到仓库，消费者直接 include 即可。

[cbindgen]: https://github.com/mozilla/cbindgen

## 验证

```bash
cargo test                        # 23 个单元测试（error/handle/version/polymesh）
python examples/verify_ctypes.py  # 通过真实 .dll 做端到端验证（阶段 2 + 3）
```

## 设计基础（所有导出类型遵循的契约）

1. **错误模型** — 可能失败的函数返回 `bool`（或可空 handle），并可选择通过
   `TruckError` handle（`*mut *mut TruckError` 出参）报告详细信息。panic **绝不**
   跨越 FFI 边界：每个 `extern "C"` 函数体都在 `catch_unwind` 保护下运行（见
   `src/error.rs`）。

2. **所有权** — truck 对象隐藏在**不透明句柄**（opaque handle）后面
   （`typedef struct TruckX TruckX;`）；C 侧永远看不到其内存布局。返回的数组/字符串
   是 `{ptr, len}` 视图，指向 Rust 分配器分配的内存，各自有对应的 `*_free` 释放函数。
   **绝对不要**用 `free()`/`delete` 释放它们 — 它们属于 Rust 分配器。每个 `*_free`
   都是幂等的且 NULL 安全。

3. **ABI 版本** — `truck_abi_version()` + `TRUCK_BRIDGE_ABI_VERSION` 宏让消费者能
   在加载时拒绝不匹配的 `.dll`。任何 C 可见布局/语义的变更都会递增此版本号。

4. **自动生成的头文件** — `truck_bridge.h` 由 cbindgen 生成；请勿手动编辑。

## 当前 API 覆盖（阶段 3）

`PolygonMesh` — 阶段 2 的只读最小子集，加上阶段 3 的完整 IO：

```c
// 生命周期
TruckPolygonMesh *truck_polygonmesh_new_empty(void);
void truck_polygonmesh_free(TruckPolygonMesh *mesh);

// 查询
bool truck_polygonmesh_bounding_box(const TruckPolygonMesh *mesh,
                                    TruckF64Array *out); // [min_xyz, max_xyz]

// IO — 每个可失败函数都带 err: *mut *mut TruckError 出参
bool truck_polygonmesh_from_obj (const uint8_t *data, size_t len,
                                 TruckPolygonMesh **out, TruckError **err);
bool truck_polygonmesh_from_stl (const uint8_t *data, size_t len, TruckStlType,
                                 TruckPolygonMesh **out, TruckError **err);
bool truck_polygonmesh_to_obj   (const TruckPolygonMesh *, TruckU8Array *out, TruckError **err);
bool truck_polygonmesh_to_stl   (const TruckPolygonMesh *, TruckStlType,
                                 TruckU8Array *out, TruckError **err);

// GPU 上传 — 分离数组（刻意偏离 truck-js 的交错 f32 格式）
bool truck_polygonmesh_to_buffer(const TruckPolygonMesh *,
                                 TruckPolygonBuffer *out, TruckError **err);
//   positions: f64 xyz | uv: f32 | normal: f32 | indices: u32

// 合并 — src 在调用中被 CONSUME（被释放）
bool truck_polygonmesh_merge(TruckPolygonMesh *dst, TruckPolygonMesh *src, TruckError **err);
```

以及基础 API：`truck_error_*`、`truck_f64array_free`、
`truck_f32array_free`、`truck_u8array_free`、`truck_u32array_free`、
`truck_polygonbuffer_free`、`truck_str_free`、`truck_abi_version`、
`truck_version_string`。

## 路线图

| 阶段 | 新增内容 | 状态 |
|---|---|---|
| 3 | `PolygonMesh` 完整功能：`from_obj` / `from_stl` / `to_obj` / `to_stl` / `to_buffer` / `merge` | ✅ 已完成 |
| 4 | 拓扑：`Vertex/Edge/Wire/Face/Shell/Solid` + `AbstractShape` + builder/transform/sweep | ⬜ 下一步 |
| 5 | 布尔运算：`and` / `or` / `not` | ⬜ |
| 6 | STEP I/O | ⬜ |

导出的 API 镜像了 [`truck-js`] 的设计决策（类型单态化、`AbstractShape` 枚举分发、
在边界处通过 `.to_polygon(tol)` 将复杂几何降级），以保证三个端口在概念上保持一致。

[`truck-js`]: https://github.com/ricosjp/truck/tree/master/truck-js

## 贡献者约定

- **禁止使用 `let ... else`** — cbindgen 0.x 的解析器不支持该语法。请改用
  `match { Some => .., None => .. }`。
- 每个 `extern "C"` 函数体：先校验 NULL 输入，再执行业务逻辑。
- 固定 truck crate 版本（`=x.y.z`）；升级 truck 是一项需要审慎对待的操作。
