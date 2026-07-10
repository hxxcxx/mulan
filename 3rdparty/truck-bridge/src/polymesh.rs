//! `PolygonMesh` 的 C ABI 接口层。
//!
//! 阶段 2 提供最小只读功能（`new_empty`、`bounding_box`、`free`）。
//! 阶段 3（本文件）补充完整的 IO 接口：
//!   - `from_obj` / `to_obj` — Wavefront OBJ 解析与序列化
//!   - `from_stl` / `to_stl` — STL 解析与序列化（ASCII/二进制）
//!   - `to_buffer` — 用于上传到 GPU 的扁平化独立数组
//!   - `merge` — 合并两个网格
//!
//! ## 错误约定
//!
//! 每个可能失败的函数都接收 `err: *mut *mut TruckError` 输出参数并返回
//! `bool`。成功时，将结果写入对应输出参数并返回 `true`。失败时，仅当 `err`
//! 本身非 NULL 才将错误句柄写入 `*err`，随后返回 `false`。所有函数体均在
//! [`truck_guard!`] 保护下运行，使 panic 转换为错误而不是发生展开。
//!
//! ## 禁止使用 `let ... else`
//!
//! cbindgen 0.x 的解析器不支持 `let ... else`，请使用 `match`。此规则适用于
//! 整个 crate。

use crate::error::{self, TruckError};
use crate::handle::{
    self, TruckF32Array, TruckF64Array, TruckU32Array, TruckU8Array,
};

// truck 的具体网格类型，由 lib.rs 重新导出，作为各处统一使用的
// `PolygonMesh<StandardVertex, StandardAttributes>` 单态化形式。

/// truck `PolygonMesh` 的不透明句柄（单态化为默认的
/// `<StandardVertex, StandardAttributes>` 形式，也是 truck 实际使用的唯一形式）。
/// C 只能看到 `typedef struct TruckPolygonMesh ...;`。
#[derive(Debug)]
pub struct TruckPolygonMesh(pub(crate) crate::PolygonMesh);

/// STL 格式选择器。各值必须通过下面的转换与
/// [`truck_polymesh::stl::StlType`] 保持一一对应；显式的 `From` 实现是唯一
/// 事实来源（不要依赖判别值的顺序）。
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TruckStlType {
    /// 读取时根据头部字节自动检测；写入时始终使用二进制格式。
    Automatic = 0,
    /// ASCII STL。
    Ascii = 1,
    /// 二进制 STL。
    Binary = 2,
}

impl From<TruckStlType> for truck_polymesh::stl::StlType {
    fn from(t: TruckStlType) -> Self {
        use truck_polymesh::stl::StlType;
        match t {
            TruckStlType::Automatic => StlType::Automatic,
            TruckStlType::Ascii => StlType::Ascii,
            TruckStlType::Binary => StlType::Binary,
        }
    }
}

/// 用于上传到 GPU 的扁平化、**彼此独立**的逐顶点数据。
///
/// 此处有意不同于 `truck-js`（后者将 pos+uv+normal 交错存储在一个 `f32`
/// 数据流中）：truck-bridge 返回四个独立数组，使用方可按其渲染器需求打包。
///
/// 由 [`truck_polygonmesh_to_buffer`] 生成时满足以下不变量：
///   - `positions.len == 3 * vertex_count`（每个顶点对应 xyz）
///   - `uv.len == 2 * vertex_count`
///   - `normal.len == 3 * vertex_count`
///   - `indices.len == 3 * triangle_count`
///
/// 空网格满足 `vertex_count == 0`，此时四个数组均为空。
/// 使用 [`truck_polygonbuffer_free`] 释放整个结构体。
#[repr(C)]
#[derive(Debug)]
pub struct TruckPolygonBuffer {
    /// 顶点位置，按 xyz 打包：`[x0,y0,z0, x1,y1,z1, ...]`。
    pub positions: TruckF64Array,
    /// 纹理坐标，按 uv 打包：`[u0,v0, u1,v1, ...]`。
    pub uv: TruckF32Array,
    /// 顶点法线，按 xyz 打包。
    pub normal: TruckF32Array,
    /// 三角形索引列表，每个三角形包含 3 个索引。
    pub indices: TruckU32Array,
}

// ---------------------------------------------------------------------------
// 结果传递辅助代码
// ---------------------------------------------------------------------------

// `truck_deliver!` 定义于 `error.rs` 并在 crate 根目录导出
//（`crate::truck_deliver!`），使所有模块共享同一条错误传递路径。

/// 将可能失败的 truck 操作包装成供 `truck_guard!` 使用的错误转换闭包。
/// 把 `Result<_, E: Display>` 映射为 `Result<_, TruckError>`。
fn lift<E: std::fmt::Display, T>(r: Result<T, E>) -> Result<T, TruckError> {
    r.map_err(|e| TruckError::new(format!("{e}")))
}

// ---------------------------------------------------------------------------
// 阶段 2 API（保留）
// ---------------------------------------------------------------------------

/// 创建空多边形网格（没有位置和面）。
///
/// 返回拥有所有权的句柄；使用 [`truck_polygonmesh_free`] 释放。
#[no_mangle]
pub extern "C" fn truck_polygonmesh_new_empty() -> *mut TruckPolygonMesh {
    handle::into_raw(TruckPolygonMesh(crate::PolygonMesh::default()))
}

/// 计算轴对齐包围盒，并以六个 `f64` 将其写入 `out`：
/// `[min_x, min_y, min_z, max_x, max_y, max_z]`.
///
/// 成功时返回 `true`；`mesh` 或 `out` 为 NULL 时返回 `false`。
///
/// 对于空网格，truck 会报告反向包围盒（`min = +INF`、`max = -INF`）；这些值
/// 会原样传递，使使用方能够检测空网格。
///
/// 返回数组的所有权归调用方，必须使用
/// [`crate::handle::truck_f64array_free`] 释放。
///
/// # 安全性
/// `mesh` 必须为 NULL 或有效句柄；`out` 必须是指向 `TruckF64Array` 的有效
/// 指针，该值将被覆盖。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_bounding_box(
    mesh: *const TruckPolygonMesh,
    out: *mut TruckF64Array,
) -> bool {
    // SAFETY: 调用方保证 mesh 为 NULL 或有效句柄。
    let m = match unsafe { handle::from_ref(mesh) } {
        Some(m) => m,
        None => return false,
    };
    // SAFETY: 调用方保证 out 为有效指针或 NULL。
    let out_ref = match unsafe { handle::from_mut(out) } {
        Some(o) => o,
        None => return false,
    };

    let bdd = m.0.bounding_box();
    let min = bdd.min();
    let max = bdd.max();
    let data = vec![min[0], min[1], min[2], max[0], max[1], max[2]];
    *out_ref = TruckF64Array::from(data);
    true
}

/// 释放多边形网格句柄。此操作具有幂等性：`truck_polygonmesh_free(NULL)`
/// 不执行任何操作。
///
/// # 安全性
/// `mesh` 必须为 NULL，或者是先前由 truck-bridge 返回且尚未释放的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_free(mesh: *mut TruckPolygonMesh) {
    // SAFETY: 调用方保证 mesh 为 NULL 或有效且拥有所有权的句柄。
    match unsafe { handle::take_raw(mesh) } {
        Some(m) => drop(m),
        None => {}
    }
}

// ---------------------------------------------------------------------------
// 阶段 3 API — IO
// ---------------------------------------------------------------------------

/// 将 Wavefront OBJ 字节解析为新网格。
///
/// 成功时将新句柄写入 `*out_mesh`（调用方使用 [`truck_polygonmesh_free`]
/// 释放）；失败时，如果 `err` 非 NULL，则将错误句柄写入 `*err`（使用
/// [`truck_error_free`] 释放），并返回 `false`。
///
/// # 安全性
/// `data` 必须可供读取 `len` 个字节（仅当 `len == 0` 时允许为 NULL）。
/// `out_mesh` 和 `err` 必须为有效可写指针或 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_from_obj(
    data: *const u8,
    len: usize,
    out_mesh: *mut *mut TruckPolygonMesh,
    err: *mut *mut TruckError,
) -> bool {
    if out_mesh.is_null() {
        return false;
    }
    let bytes: &[u8] = if data.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: 调用方保证 data 可供读取 len 个字节。
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    let res = error::truck_guard!(|| lift(truck_polymesh::obj::read::<&[u8]>(bytes)));
    crate::truck_deliver!(res, err, |m: crate::PolygonMesh| {
        // SAFETY: 上方已检查 out_mesh 非 NULL。
        unsafe { *out_mesh = handle::into_raw(TruckPolygonMesh(m)) };
    })
}

/// 将 STL 字节解析为新网格。输出参数/错误契约参见
/// [`truck_polygonmesh_from_obj`]。
///
/// # 安全性
/// 与 [`truck_polygonmesh_from_obj`] 相同。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_from_stl(
    data: *const u8,
    len: usize,
    stl_type: TruckStlType,
    out_mesh: *mut *mut TruckPolygonMesh,
    err: *mut *mut TruckError,
) -> bool {
    if out_mesh.is_null() {
        return false;
    }
    let bytes: &[u8] = if data.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: 调用方保证 data 可供读取 len 个字节。
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    let res = error::truck_guard!(|| {
        lift(truck_polymesh::stl::read::<&[u8]>(bytes, stl_type.into()))
    });
    crate::truck_deliver!(res, err, |m: crate::PolygonMesh| {
        // SAFETY: 上方已检查 out_mesh 非 NULL。
        unsafe { *out_mesh = handle::into_raw(TruckPolygonMesh(m)) };
    })
}

/// 将网格序列化为 Wavefront OBJ 字节。
///
/// 成功时将字节写入 `*out`（调用方使用 [`truck_u8array_free`] 释放）；
/// 失败时将错误句柄写入 `*err`。
///
/// # 安全性
/// `mesh` 必须为 NULL 或有效句柄。`out` 和 `err` 必须为有效可写指针或 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_to_obj(
    mesh: *const TruckPolygonMesh,
    out: *mut TruckU8Array,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 mesh 为 NULL 或有效句柄。
    let m = match unsafe { handle::from_ref(mesh) } {
        Some(m) => m,
        None => return false,
    };
    let res = error::truck_guard!(|| {
        let mut buf = Vec::new();
        lift(truck_polymesh::obj::write(&m.0, &mut buf))?;
        Ok::<_, TruckError>(buf)
    });
    crate::truck_deliver!(res, err, |buf: Vec<u8>| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = TruckU8Array::from(buf) };
    })
}

/// 将网格序列化为 STL 字节。输出参数/错误契约参见
/// [`truck_polygonmesh_to_obj`]。`StlType::Automatic` 会写入二进制格式。
///
/// # 安全性
/// 与 [`truck_polygonmesh_to_obj`] 相同。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_to_stl(
    mesh: *const TruckPolygonMesh,
    stl_type: TruckStlType,
    out: *mut TruckU8Array,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 mesh 为 NULL 或有效句柄。
    let m = match unsafe { handle::from_ref(mesh) } {
        Some(m) => m,
        None => return false,
    };
    let res = error::truck_guard!(|| {
        let mut buf = Vec::new();
        lift(truck_polymesh::stl::write(&m.0, &mut buf, stl_type.into()))?;
        Ok::<_, TruckError>(buf)
    });
    crate::truck_deliver!(res, err, |buf: Vec<u8>| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = TruckU8Array::from(buf) };
    })
}

// ---------------------------------------------------------------------------
// 阶段 3 API — 缓冲区与合并
// ---------------------------------------------------------------------------

/// 将网格扁平化为彼此独立的逐顶点数组，以便上传到 GPU。
///
/// 生成包含独立 `positions` (f64)、`uv` (f32)、`normal` (f32) 和 `indices`
/// (u32) 的 [`TruckPolygonBuffer`]。网格会被完全展开：每个三角形顶点都作为
/// 独立属性项输出，因此三个属性数组共享同一索引空间，并且都包含
/// `vertex_count` 个逻辑顶点（`positions.len == 3 * vertex_count` 等）。
///
/// 使用 [`truck_polygonbuffer_free`] 释放结果。
///
/// # 安全性
/// `mesh` 必须为 NULL 或有效句柄。`out` 和 `err` 必须为有效可写指针或 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_to_buffer(
    mesh: *const TruckPolygonMesh,
    out: *mut TruckPolygonBuffer,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 mesh 为 NULL 或有效句柄。
    let m = match unsafe { handle::from_ref(mesh) } {
        Some(m) => m,
        None => return false,
    };
    let res = error::truck_guard!(|| {
        // 将每个面顶点展开为独立的 (pos, uv, normal) 元组。
        let exp = m.0.expands(|attr| {
            let p = attr.position;
            let uv = attr.uv_coord.unwrap_or_else(|| {
                truck_polymesh::base::Vector2::new(0.0, 0.0)
            });
            let n = attr.normal.unwrap_or_else(|| {
                truck_polymesh::base::Vector3::new(0.0, 0.0, 0.0)
            });
            (p, uv, n)
        });
        let n_vert = exp.attributes().len();
        let mut positions = Vec::with_capacity(n_vert * 3);
        let mut uv = Vec::with_capacity(n_vert * 2);
        let mut normal = Vec::with_capacity(n_vert * 3);
        for (p, t, nm) in exp.attributes().iter() {
            positions.push(p[0] as f64);
            positions.push(p[1] as f64);
            positions.push(p[2] as f64);
            uv.push(t[0] as f32);
            uv.push(t[1] as f32);
            normal.push(nm[0] as f32);
            normal.push(nm[1] as f32);
            normal.push(nm[2] as f32);
        }
        let indices: Vec<u32> = exp.faces().triangle_iter().flatten().map(|i| i as u32).collect();
        Ok::<_, TruckError>(TruckPolygonBuffer {
            positions: TruckF64Array::from(positions),
            uv: TruckF32Array::from(uv),
            normal: TruckF32Array::from(normal),
            indices: TruckU32Array::from(indices),
        })
    });
    crate::truck_deliver!(res, err, |buf: TruckPolygonBuffer| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = buf };
    })
}

/// 将 `src` 原地合并到 `dst`。
///
/// **所有权：** `src` 会被*消费*，本次调用会释放其句柄。之后不得再使用或
/// 释放 `src`（若句柄为 NULL，由于 `take_raw`/`*_free` 允许 NULL，再次释放
/// 不会产生影响；但它原先持有的网格现已成为 `dst` 的一部分）。
///
/// 成功时返回 `true`，任一句柄为 NULL 时返回 `false`。
///
/// # 安全性
/// `dst` 和 `src` 必须分别为 NULL 或有效句柄。本次调用后不得再访问 `src`。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonmesh_merge(
    dst: *mut TruckPolygonMesh,
    src: *mut TruckPolygonMesh,
    err: *mut *mut TruckError,
) -> bool {
    // SAFETY: 调用方保证 dst 为 NULL 或有效句柄。
    let dst_ref = match unsafe { handle::from_mut(dst) } {
        Some(d) => d,
        None => return false,
    };
    // 消费 src：取得其内部 PolygonMesh 的所有权（同时丢弃句柄）。
    // SAFETY: 调用方保证 src 为 NULL 或有效且拥有所有权的句柄。
    let src_mesh = match unsafe { handle::take_raw(src) } {
        Some(s) => s.0,
        None => return false,
    };
    let res = error::truck_guard!(|| {
        dst_ref.0.merge(src_mesh);
        Ok::<_, TruckError>(())
    });
    crate::truck_deliver!(res, err, |_v: ()| {})
}

/// 释放 [`TruckPolygonBuffer`] 及其四个内部数组。此操作具有幂等性，每个数组
/// 都允许为空。
///
/// # 安全性
/// `buf` 必须描述先前由 truck-bridge 生成的内存分配（每个数组均可为空值），
/// 并且不得已经被释放。
#[no_mangle]
pub unsafe extern "C" fn truck_polygonbuffer_free(buf: TruckPolygonBuffer) {
    // SAFETY: 每个数组均来自 Truck*Array::from（或为空），且 cap == len。
    unsafe {
        crate::handle::truck_f32array_free(buf.normal);
        crate::handle::truck_f32array_free(buf.uv);
        crate::handle::truck_u32array_free(buf.indices);
        crate::handle::truck_f64array_free(buf.positions);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 仅含一个三角形的最小 OBJ。
    const TRI_OBJ: &[u8] = b"v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1 2 3\n";

    fn from_obj(bytes: &[u8]) -> *mut TruckPolygonMesh {
        let mut out: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: bytes 是有效切片；out/err 是有效指针。
        let ok = unsafe {
            truck_polygonmesh_from_obj(bytes.as_ptr(), bytes.len(), &mut out, &mut err)
        };
        assert!(ok, "from_obj failed unexpectedly");
        assert!(out != std::ptr::null_mut());
        assert!(err.is_null());
        out
    }

    #[test]
    fn obj_roundtrip_preserves_positions() {
        let m1 = from_obj(TRI_OBJ);
        // 重新序列化
        let mut bytes = TruckU8Array { ptr: std::ptr::null_mut(), len: 0 };
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: m1、bytes/err 均有效。
        let ok = unsafe { truck_polygonmesh_to_obj(m1, &mut bytes, &mut err) };
        assert!(ok);
        assert!(bytes.len > 0);
        // 重新解析
        // SAFETY: bytes.ptr 可供读取 bytes.len 个字节。
        let slice = unsafe { std::slice::from_raw_parts(bytes.ptr, bytes.len) };
        let m2 = from_obj(slice);
        // 通过包围盒比较位置（对于此小型网格已足够）
        let mut b1 = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        let mut b2 = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        unsafe {
            truck_polygonmesh_bounding_box(m1, &mut b1);
            truck_polygonmesh_bounding_box(m2, &mut b2);
        }
        // SAFETY: 指针分别可供读取 b1.len/b2.len 个元素。
        let s1 = unsafe { std::slice::from_raw_parts(b1.ptr, b1.len) };
        let s2 = unsafe { std::slice::from_raw_parts(b2.ptr, b2.len) };
        assert_eq!(s1, s2);
        unsafe {
            crate::handle::truck_f64array_free(b1);
            crate::handle::truck_f64array_free(b2);
            crate::handle::truck_u8array_free(bytes);
            truck_polygonmesh_free(m1);
            truck_polygonmesh_free(m2);
        }
    }

    #[test]
    fn from_obj_garbage_yields_empty_mesh() {
        // OBJ 解析较为宽松：无法识别的行会被跳过，因此无效文本会被解析为
        // 空网格而不是错误。此测试用于记录该行为。
        let mut out: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        let garbage = b"not an obj at all!!!";
        // SAFETY: garbage 是有效切片；out/err 有效。
        let ok = unsafe {
            truck_polygonmesh_from_obj(garbage.as_ptr(), garbage.len(), &mut out, &mut err)
        };
        assert!(ok, "garbage OBJ should parse to an empty mesh, not fail");
        assert!(err.is_null());
        // 结果网格没有顶点，因此包围盒是反向无穷包围盒。
        let mut bbox = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        unsafe {
            truck_polygonmesh_bounding_box(out, &mut bbox);
            crate::handle::truck_f64array_free(bbox);
            truck_polygonmesh_free(out);
        }
    }

    #[test]
    fn from_stl_truncated_yields_error() {
        // 截断的二进制 STL（字节数不足以构成 84 字节头部）是真正的解析错误，
        // 用于覆盖 err 输出参数路径。
        let mut out: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        let truncated = b"\x00\x00\x00\x00truncated";
        // SAFETY: truncated 是有效切片；out/err 有效。
        let ok = unsafe {
            truck_polygonmesh_from_stl(
                truncated.as_ptr(),
                truncated.len(),
                TruckStlType::Binary,
                &mut out,
                &mut err,
            )
        };
        assert!(!ok, "truncated STL should fail to parse");
        assert!(out.is_null());
        assert!(!err.is_null(), "an error handle should be produced");
        // 错误必须携带非空消息。
        // SAFETY: err 有效且拥有所有权。
        let s = unsafe { crate::error::truck_error_message(err) };
        // SAFETY: s.ptr 可供读取 s.len 个字节。
        let mbytes = unsafe { std::slice::from_raw_parts(s.ptr, s.len) };
        assert!(!mbytes.is_empty(), "error message should be non-empty");
        unsafe {
            crate::handle::truck_str_free(s);
            crate::error::truck_error_free(err);
        }
    }

    #[test]
    fn to_buffer_single_triangle() {
        let m = from_obj(TRI_OBJ);
        let mut buf = TruckPolygonBuffer {
            positions: TruckF64Array { ptr: std::ptr::null_mut(), len: 0 },
            uv: TruckF32Array { ptr: std::ptr::null_mut(), len: 0 },
            normal: TruckF32Array { ptr: std::ptr::null_mut(), len: 0 },
            indices: TruckU32Array { ptr: std::ptr::null_mut(), len: 0 },
        };
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: m、buf/err 均有效。
        let ok = unsafe { truck_polygonmesh_to_buffer(m, &mut buf, &mut err) };
        assert!(ok);
        assert_eq!(buf.positions.len, 9); // 3 个顶点 * 3
        assert_eq!(buf.uv.len, 6); // 3 个顶点 * 2
        assert_eq!(buf.normal.len, 9);
        assert_eq!(buf.indices.len, 3); // 1 个三角形
        unsafe {
            truck_polygonbuffer_free(buf);
            truck_polygonmesh_free(m);
        }
    }

    #[test]
    fn merge_combines_two_meshes() {
        let a = from_obj(TRI_OBJ);
        let b = from_obj(TRI_OBJ);
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: a、b 是有效句柄；err 有效。
        let ok = unsafe { truck_polygonmesh_merge(a, b, &mut err) };
        assert!(ok);
        // 合并后网格的缓冲区应包含 2 个三角形
        let mut buf = TruckPolygonBuffer {
            positions: TruckF64Array { ptr: std::ptr::null_mut(), len: 0 },
            uv: TruckF32Array { ptr: std::ptr::null_mut(), len: 0 },
            normal: TruckF32Array { ptr: std::ptr::null_mut(), len: 0 },
            indices: TruckU32Array { ptr: std::ptr::null_mut(), len: 0 },
        };
        let mut err2: *mut TruckError = std::ptr::null_mut();
        unsafe {
            truck_polygonmesh_to_buffer(a, &mut buf, &mut err2);
            assert_eq!(buf.indices.len, 6); // 2 个三角形
            truck_polygonbuffer_free(buf);
            truck_polygonmesh_free(a); // b 已被 merge 消费
        }
    }

    #[test]
    fn stl_binary_roundtrip() {
        let m1 = from_obj(TRI_OBJ);
        // 转换为二进制 STL
        let mut bytes = TruckU8Array { ptr: std::ptr::null_mut(), len: 0 };
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: m1 有效。
        let ok = unsafe {
            truck_polygonmesh_to_stl(m1, TruckStlType::Binary, &mut bytes, &mut err)
        };
        assert!(ok);
        assert!(bytes.len > 0);
        // 从二进制 STL 转换
        // SAFETY: bytes.ptr 可供读取 bytes.len 个字节。
        let slice = unsafe { std::slice::from_raw_parts(bytes.ptr, bytes.len) };
        let mut m2_raw: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err2: *mut TruckError = std::ptr::null_mut();
        // SAFETY: slice 有效。
        let ok2 = unsafe {
            truck_polygonmesh_from_stl(
                slice.as_ptr(),
                slice.len(),
                TruckStlType::Binary,
                &mut m2_raw,
                &mut err2,
            )
        };
        assert!(ok2);
        assert!(!m2_raw.is_null());
        unsafe {
            crate::handle::truck_u8array_free(bytes);
            truck_polygonmesh_free(m1);
            truck_polygonmesh_free(m2_raw);
        }
    }

    // ---- 阶段 2 回归测试（保留）----

    #[test]
    fn new_empty_is_non_null() {
        let m = truck_polygonmesh_new_empty();
        assert!(!m.is_null());
        // SAFETY: m 来自 new_empty，尚未释放。
        unsafe { truck_polygonmesh_free(m) };
    }

    #[test]
    fn empty_mesh_bounding_box_is_inverted_infinities() {
        let m = truck_polygonmesh_new_empty();
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: m 是新句柄；arr 是有效输出指针。
        let ok = unsafe { truck_polygonmesh_bounding_box(m, &mut arr) };
        assert!(ok);
        assert_eq!(arr.len, 6);
        // SAFETY: arr 刚刚生成，arr.ptr 可供读取 arr.len 个元素。
        let s = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert!(s[0].is_infinite() && s[0].is_sign_positive());
        assert!(s[3].is_infinite() && s[3].is_sign_negative());
        unsafe {
            crate::handle::truck_f64array_free(arr);
            truck_polygonmesh_free(m);
        }
    }

    #[test]
    fn null_arguments_return_false() {
        let m = truck_polygonmesh_new_empty();
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        assert!(!unsafe { truck_polygonmesh_bounding_box(std::ptr::null(), &mut arr) });
        assert!(!unsafe { truck_polygonmesh_bounding_box(m, std::ptr::null_mut()) });
        unsafe { truck_polygonmesh_free(m) };
    }

    #[test]
    fn free_null_is_safe() {
        // SAFETY: 明确允许传入 NULL。
        unsafe { truck_polygonmesh_free(std::ptr::null_mut()) };
    }
}
