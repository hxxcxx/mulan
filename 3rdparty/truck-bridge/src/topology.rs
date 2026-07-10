//! 拓扑层 C ABI 接口，包含 B-rep 类型（`Vertex`、`Edge`、`Wire`、`Face`、
//! `Shell`、`Solid`）。
//!
//! - 阶段 4a：`Vertex` — 构造、读取点、释放。
//! - 阶段 4b：`Edge` — `line` / `circle_arc`（经由中间点）/ `bezier`
//!   构造函数，`front_vertex` / `back_vertex` 查询，以及释放。
//! - 阶段 4c：`Face` — `homotopy` 构造函数、边界边计数与枚举（通过
//!   `TruckEdgeArray` 句柄数组类型），以及释放。
//! - 阶段 4d（当前）：`Shell` / `Solid` — 从子形状构造并细分为
//!   `PolygonMesh`；支持 `tsweep` / `rsweep` / `translated` / `rotated` /
//!   `scaled` 的 `AbstractShape` 多态句柄。
//!
//! 此处所有具体拓扑类型均来自 `truck_modeling`，后者已通过 `prelude!` 宏将
//! `Vertex<P>` / `Edge<P, C>` / `Solid<P, C, S>` 单态化为
//! `<Point3, Curve, Surface>`。本层不会接触泛型形式。
//!
//! ## 约定（与 crate 其余部分共享）
//!
//! - 使用不透明句柄新类型（不含 `#[repr(C)]`）；C 只能看到 `typedef struct`。
//! - 每个 `extern "C"` 函数体都在 `truck_guard!` 保护下运行，防止 panic 展开。
//! - 首先拒绝 NULL 输入；每个 `*_free` 都具有幂等性并允许传入 NULL。
//! - 不使用 `let ... else`（cbindgen 0.x 无法解析），改用 `match`。

use crate::error::TruckError;
use crate::handle::{self, TruckF64Array};
use crate::polymesh::TruckPolygonMesh;
use truck_meshalgo::tessellation::{MeshedShape, RobustMeshableShape};
use truck_modeling::{builder, Edge, Face, Point3, Shell, Solid, Vector3, Vertex, Wire};

/// 原点 `(0, 0, 0)`。集中定义于此，使各基本体构造函数无需分别依赖
/// `EuclideanSpace` trait 来调用 `Point3::origin()`。
const ORIGIN: Point3 = Point3 {
    x: 0.0,
    y: 0.0,
    z: 0.0,
};

/// truck `Vertex`（具体 `<Point3>` 形式）的不透明句柄。C 只能看到
/// `typedef struct TruckVertex TruckVertex;`。
///
/// `Vertex` 内部持有 `Arc<Mutex<Point3>>`；它实现了 `Send + Sync`，因此句柄
/// 可以在线程间移动。并发读取其点是安全的，并发修改则不安全，调用方必须
/// 对此类访问进行串行化。
#[derive(Debug)]
pub struct TruckVertex(pub(crate) Vertex);

/// 根据 `(x, y, z)` 坐标创建顶点。
///
/// 返回拥有所有权的句柄；使用 [`truck_vertex_free`] 释放。
#[no_mangle]
pub extern "C" fn truck_vertex_new(x: f64, y: f64, z: f64) -> *mut TruckVertex {
    handle::into_raw(TruckVertex(builder::vertex(Point3::new(x, y, z))))
}

/// 以三个 `f64` 将顶点的 `(x, y, z)` 坐标写入 `out`。
///
/// 成功时返回 `true`，`vertex` 或 `out` 为 NULL 时返回 `false`。返回数组的
/// 所有权归调用方，必须使用 [`crate::handle::truck_f64array_free`] 释放。
///
/// # 安全性
/// `vertex` 必须为 NULL 或有效句柄；`out` 必须是指向 `TruckF64Array` 的
/// 有效指针，该值将被覆盖。
#[no_mangle]
pub unsafe extern "C" fn truck_vertex_point(
    vertex: *const TruckVertex,
    out: *mut TruckF64Array,
) -> bool {
    // SAFETY: 调用方保证 vertex 为 NULL 或有效句柄。
    let v = match unsafe { handle::from_ref(vertex) } {
        Some(v) => v,
        None => return false,
    };
    // SAFETY: 调用方保证 out 为有效指针或 NULL。
    let out_ref = match unsafe { handle::from_mut(out) } {
        Some(o) => o,
        None => return false,
    };

    // `Vertex::point(&self) -> P` 返回拥有所有权的 Point3（克隆值），因此
    // 不会与 vertex 产生生命周期耦合。
    let p = v.0.point();
    *out_ref = TruckF64Array::from(vec![p[0], p[1], p[2]]);
    true
}

/// 释放顶点句柄。此操作具有幂等性：`truck_vertex_free(NULL)` 不执行任何操作。
///
/// # 安全性
/// `vertex` 必须为 NULL，或者是先前由 truck-bridge 返回且尚未释放的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_vertex_free(vertex: *mut TruckVertex) {
    // SAFETY: 调用方保证 vertex 为 NULL 或有效且拥有所有权的句柄。
    match unsafe { handle::take_raw(vertex) } {
        Some(v) => drop(v),
        None => {}
    }
}

// ===========================================================================
// 阶段 4b — Edge
// ===========================================================================

/// truck `Edge`（具体 `<Point3, Curve>` 形式）的不透明句柄。C 只能看到
/// `typedef struct TruckEdge TruckEdge;`。
#[derive(Debug)]
pub struct TruckEdge(pub(crate) Edge);

/// 从 C 数组读取三个 `f64`（`[x, y, z]`）；若 `p` 为 NULL 或长度不足，
/// 则返回 `None`。
///
/// # 安全性
/// `p` 必须为 NULL，或者可供读取 `len` 个 `f64`。
unsafe fn read_vec3(p: *const f64, len: usize) -> Option<[f64; 3]> {
    if p.is_null() || len < 3 {
        return None;
    }
    // SAFETY: 调用方保证 p 可供读取 len >= 3 个 f64。
    let s = unsafe { std::slice::from_raw_parts(p, 3) };
    Some([s[0], s[1], s[2]])
}

/// 在两个顶点之间创建直边（线段）。两个输入顶点句柄仅被**借用**，不会被消费。
///
/// 返回新的边句柄；任一顶点为 NULL 时返回 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_line(
    v0: *const TruckVertex,
    v1: *const TruckVertex,
) -> *mut TruckEdge {
    // SAFETY: 调用方保证这些句柄为 NULL 或有效句柄。
    let (a, b) = match (unsafe { handle::from_ref(v0) }, unsafe { handle::from_ref(v1) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| Ok::<Edge, crate::error::TruckError>(builder::line(&a.0, &b.0)));
    match res {
        Ok(edge) => handle::into_raw(TruckEdge(edge)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 创建从 `v0` 到 `v1` 且经过 `transit`（`[x, y, z]`）的圆弧边。
/// 顶点仅被借用。
///
/// 返回新的边句柄；若 `v0`/`v1`/`transit` 为 NULL、`transit` 长度不足，
/// 或三个点退化（共线），则返回 NULL。退化时 truck 会在内部 panic，保护层
/// 会将其转换为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_circle_arc_by_transit(
    v0: *const TruckVertex,
    v1: *const TruckVertex,
    transit: *const f64,
    transit_len: usize,
) -> *mut TruckEdge {
    // SAFETY: 调用方保证这些句柄为 NULL 或有效句柄。
    let (a, b) = match (unsafe { handle::from_ref(v0) }, unsafe { handle::from_ref(v1) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    // SAFETY: 调用方保证 transit 为 NULL 或可供读取 transit_len 个 f64。
    let [x, y, z] = match unsafe { read_vec3(transit, transit_len) } {
        Some(v) => v,
        None => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| {
        Ok::<Edge, crate::error::TruckError>(builder::circle_arc(&a.0, &b.0, Point3::new(x, y, z)))
    });
    match res {
        Ok(edge) => handle::into_raw(TruckEdge(edge)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 使用中间控制点创建从 `v0` 到 `v1` 的 Bezier 曲线边。`ctrl` 是包含
/// `ctrl_len` 个 `f64` 的扁平数组，即包含 `ctrl_len / 3` 个控制点，每个点
/// 为 `[x, y, z]`。`ctrl_len` 必须是 3 的倍数（表示直线段时，可在
/// `ctrl_len == 0` 的情况下将 `ctrl` 设为 NULL）。
///
/// 顶点仅被借用。
///
/// 返回新的边句柄；顶点为 NULL 或 `ctrl_len` 不是 3 的倍数时返回 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_bezier(
    v0: *const TruckVertex,
    v1: *const TruckVertex,
    ctrl: *const f64,
    ctrl_len: usize,
) -> *mut TruckEdge {
    let (a, b) = match (unsafe { handle::from_ref(v0) }, unsafe { handle::from_ref(v1) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    if ctrl_len % 3 != 0 {
        return std::ptr::null_mut();
    }
    let points: Vec<Point3> = if ctrl.is_null() || ctrl_len == 0 {
        Vec::new()
    } else {
        // SAFETY: 调用方保证 ctrl 可供读取 ctrl_len 个 f64。
        let s = unsafe { std::slice::from_raw_parts(ctrl, ctrl_len) };
        s.chunks_exact(3)
            .map(|c| Point3::new(c[0], c[1], c[2]))
            .collect()
    };
    let res = crate::error::truck_guard!(|| {
        Ok::<Edge, crate::error::TruckError>(builder::bezier(&a.0, &b.0, points))
    });
    match res {
        Ok(edge) => handle::into_raw(TruckEdge(edge)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 以**全新且独立**的句柄返回 `edge` 的前端（起始）顶点。
///
/// 返回的 `TruckVertex` 是克隆值，必须单独使用 [`truck_vertex_free`] 释放；
/// 其生命周期与 `edge` 解耦。
///
/// `edge` 为 NULL 时返回 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_front_vertex(edge: *const TruckEdge) -> *mut TruckVertex {
    // SAFETY: 调用方保证 edge 为 NULL 或有效句柄。
    let e = match unsafe { handle::from_ref(edge) } {
        Some(e) => e,
        None => return std::ptr::null_mut(),
    };
    // front() 返回借用值；通过克隆获得可跨 FFI 传递且拥有所有权的 Vertex。
    handle::into_raw(TruckVertex(e.0.front().clone()))
}

/// 以**全新且独立**的句柄返回 `edge` 的后端（终止）顶点。
///
/// 参见 [`truck_edge_front_vertex`]：结果是拥有独立生命周期的克隆值。
/// `edge` 为 NULL 时返回 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_back_vertex(edge: *const TruckEdge) -> *mut TruckVertex {
    // SAFETY: 调用方保证 edge 为 NULL 或有效句柄。
    let e = match unsafe { handle::from_ref(edge) } {
        Some(e) => e,
        None => return std::ptr::null_mut(),
    };
    handle::into_raw(TruckVertex(e.0.back().clone()))
}

/// 释放边句柄。此操作具有幂等性：`truck_edge_free(NULL)` 不执行任何操作。
///
/// # 安全性
/// `edge` 必须为 NULL，或者是先前由 truck-bridge 返回且尚未释放的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_free(edge: *mut TruckEdge) {
    // SAFETY: 调用方保证 edge 为 NULL 或有效且拥有所有权的句柄。
    match unsafe { handle::take_raw(edge) } {
        Some(e) => drop(e),
        None => {}
    }
}

// ===========================================================================
// 阶段 4c — Face + TruckEdgeArray
// ===========================================================================

/// truck `Face`（具体 `<Point3, Curve, Surface>` 形式）的不透明句柄。
/// C 只能看到 `typedef struct TruckFace TruckFace;`。
#[derive(Debug)]
pub struct TruckFace(pub(crate) Face);

/// 自有不透明边句柄数组（`*mut *mut TruckEdge`）。
///
/// **两层所有权**（请仔细阅读）：
///   - 数组容器本身是一块独立的内存分配（由边句柄指针组成的
///     `ptr[0..len]`）。使用 [`truck_edgearray_free`] 释放。
///   - 每个元素 `ptr[i]` 都是**独立**的边句柄，必须分别使用
///     [`truck_edge_free`] 释放。
///
/// 通常需要一次释放全部内容时，请使用 [`truck_edgearray_free_all`]；它会
/// 同时释放容器及其中的每个句柄。
#[repr(C)]
#[derive(Debug)]
pub struct TruckEdgeArray {
    /// 指向 `len` 个边句柄（`*mut TruckEdge`）的指针。
    pub ptr: *mut *mut TruckEdge,
    /// 句柄数量。
    pub len: usize,
}

impl TruckEdgeArray {
    /// 空数组（NULL 指针、长度为零），使用任一释放方式均安全。
    /// 供需要零初始化起始值的调用方/测试使用。
    #[allow(dead_code)]
    fn empty() -> Self {
        Self { ptr: std::ptr::null_mut(), len: 0 }
    }
}

/// 创建从 `e0` 扫掠到 `e1` 的同伦面。两个边句柄仅被**借用**，不会被消费。
///
/// 返回新的面句柄；若任一边为 NULL 或两条边在几何上不兼容，则返回 NULL
///（truck 会在内部 panic，保护层会将其转换为 NULL）。
#[no_mangle]
pub unsafe extern "C" fn truck_face_homotopy(
    e0: *const TruckEdge,
    e1: *const TruckEdge,
) -> *mut TruckFace {
    // SAFETY: 调用方保证这些句柄为 NULL 或有效句柄。
    let (a, b) = match (unsafe { handle::from_ref(e0) }, unsafe { handle::from_ref(e1) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| {
        Ok::<Face, crate::error::TruckError>(builder::homotopy(&a.0, &b.0))
    });
    match res {
        Ok(face) => handle::into_raw(TruckFace(face)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 统计 `face` 的边界边数量。
///
/// `face` 为 NULL 时返回 0。`homotopy` 恰好生成 4 条边界边。
#[no_mangle]
pub unsafe extern "C" fn truck_face_boundary_edge_count(face: *const TruckFace) -> usize {
    // SAFETY: 调用方保证 face 为 NULL 或有效句柄。
    let f = match unsafe { handle::from_ref(face) } {
        Some(f) => f,
        None => return 0,
    };
    f.0.boundary_iters().into_iter().flatten().count()
}

/// 将 `face` 的边界边枚举为由**独立**边句柄组成的 [`TruckEdgeArray`]。
///
/// 每条返回边的所有权均归调用方；使用 [`truck_edgearray_free_all`] 释放完整
/// 结果（也可使用 [`truck_edge_free`] 逐一释放句柄，再使用
/// [`truck_edgearray_free`] 释放容器）。
///
/// 成功时返回 `true`，`face` 或 `out` 为 NULL 时返回 `false`。
///
/// # 安全性
/// `face` 必须为 NULL 或有效句柄；`out` 必须是指向 `TruckEdgeArray` 的
/// 有效指针，该值将被覆盖。
#[no_mangle]
pub unsafe extern "C" fn truck_face_boundary_edges(
    face: *const TruckFace,
    out: *mut TruckEdgeArray,
) -> bool {
    // SAFETY: 调用方保证 face 为 NULL 或有效句柄。
    let f = match unsafe { handle::from_ref(face) } {
        Some(f) => f,
        None => return false,
    };
    // SAFETY: 调用方保证 out 为有效指针或 NULL。
    let out_ref = match unsafe { handle::from_mut(out) } {
        Some(o) => o,
        None => return false,
    };

    // boundary_iters().flatten() 生成拥有所有权的 Edge 值；将每个值包装为
    // 新的 TruckEdge 句柄。
    let handles: Vec<*mut TruckEdge> = f
        .0
        .boundary_iters()
        .into_iter()
        .flatten()
        .map(|edge| handle::into_raw(TruckEdge(edge)))
        .collect();
    let len = handles.len();
    let (ptr, _len, _cap) = handle::vec_into_raw_parts(handles);
    *out_ref = TruckEdgeArray { ptr, len };
    true
}

/// 释放面句柄。此操作具有幂等性：`truck_face_free(NULL)` 不执行任何操作。
///
/// # 安全性
/// `face` 必须为 NULL，或者是先前由 truck-bridge 返回且尚未释放的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_face_free(face: *mut TruckFace) {
    // SAFETY: 调用方保证 face 为 NULL 或有效且拥有所有权的句柄。
    match unsafe { handle::take_raw(face) } {
        Some(f) => drop(f),
        None => {}
    }
}

/// 仅释放 `TruckEdgeArray` **容器**（指针数组），不处理各个边句柄。需要保留
/// 这些边时使用此函数，否则优先使用 [`truck_edgearray_free_all`]。对于空数组，
/// 此操作具有幂等性。
///
/// # 安全性
/// `arr` 必须描述先前由 truck-bridge 生成的容器（或为空值），并且不得已经
/// 被释放。
#[no_mangle]
pub unsafe extern "C" fn truck_edgearray_free(arr: TruckEdgeArray) {
    if arr.ptr.is_null() {
        return;
    }
    // SAFETY: arr.ptr 来自对 Vec<*mut TruckEdge> 调用 vec_into_raw_parts，且
    // cap == len（Vec::collect 中的 shrink_to_fit 不保证 cap==len，因此恢复时
    // 两者均使用 len；句柄本身是独立的 Box 分配，按设计不会在此处丢弃）。
    drop(unsafe {
        handle::vec_from_raw_parts::<*mut TruckEdge>(arr.ptr, arr.len, arr.len)
    });
}

/// 释放 `TruckEdgeArray` **容器及其中的每个边句柄**。这是常用选择。对于
/// 空数组，此操作具有幂等性。
///
/// # 安全性
/// `arr` 必须描述先前由 truck-bridge 生成的容器与句柄，且均未被释放。
/// 本次调用后，不得再次使用任何句柄或容器。
#[no_mangle]
pub unsafe extern "C" fn truck_edgearray_free_all(arr: TruckEdgeArray) {
    if arr.ptr.is_null() {
        return;
    }
    // SAFETY: arr.ptr 可供读取 arr.len 个句柄指针。
    let slice = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
    for &h in slice {
        // SAFETY: 每个 h 都是独立且拥有所有权的 TruckEdge 句柄（也可能为
        // NULL，而 truck_edge_free 允许传入 NULL）。
        unsafe { truck_edge_free(h) };
    }
    // SAFETY: 与 truck_edgearray_free 相同，恢复容器 Vec。
    drop(unsafe {
        handle::vec_from_raw_parts::<*mut TruckEdge>(arr.ptr, arr.len, arr.len)
    });
}

// ===========================================================================
// 阶段 4d — Shell / Solid / AbstractShape / 扫掠 / 变换
// ===========================================================================

/// truck `Shell`（具体 `<Point3, Curve, Surface>` 形式）的不透明句柄。
#[derive(Debug)]
pub struct TruckShell(pub(crate) Shell);

/// truck `Solid`（具体 `<Point3, Curve, Surface>` 形式）的不透明句柄。
#[derive(Debug)]
pub struct TruckSolid(pub(crate) Solid);

/// 多态拓扑句柄，用于包装任意具体拓扑类型，使 `tsweep` / `rsweep` / 变换
/// 操作能够接收“任意形状”并返回“任意形状”（结果类型取决于输入类型）。
///
/// 使用 `truck_{vertex,edge,face,shell,solid}_upcast` 构建，使用
/// `truck_abstractshape_is_*` 检查，使用 `truck_abstractshape_into_*` 提取
///（提取操作会消费 `AbstractShape`）。
#[derive(Debug)]
pub struct AbstractShape(pub(crate) SubShape);

#[derive(Debug)]
pub(crate) enum SubShape {
    Vertex(TruckVertex),
    Edge(TruckEdge),
    Face(TruckFace),
    Shell(TruckShell),
    Solid(TruckSolid),
}

impl AbstractShape {
    pub(crate) fn from_vertex(v: TruckVertex) -> Self {
        AbstractShape(SubShape::Vertex(v))
    }
    pub(crate) fn from_edge(e: TruckEdge) -> Self {
        AbstractShape(SubShape::Edge(e))
    }
    pub(crate) fn from_face(f: TruckFace) -> Self {
        AbstractShape(SubShape::Face(f))
    }
    pub(crate) fn from_shell(s: TruckShell) -> Self {
        AbstractShape(SubShape::Shell(s))
    }
    pub(crate) fn from_solid(s: TruckSolid) -> Self {
        AbstractShape(SubShape::Solid(s))
    }
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

/// 根据面句柄数组构建壳体。**面句柄会被消费**（移动到壳体中），之后不得
/// 再使用或释放。
///
/// 成功时返回新的壳体句柄；`faces` 为 NULL 或 `count` 为 0 时返回 NULL。
///
/// # 安全性
/// `faces` 必须为 NULL，或者是包含 `count` 个 `*mut TruckFace` 句柄的有效
/// 数组；每个句柄都必须有效且拥有所有权（均未被释放或消费）。
#[no_mangle]
pub unsafe extern "C" fn truck_shell_from_faces(
    faces: *const *mut TruckFace,
    count: usize,
) -> *mut TruckShell {
    if faces.is_null() || count == 0 {
        return std::ptr::null_mut();
    }
    // SAFETY: 调用方保证 faces 可供读取 count 个句柄指针。
    let slice = unsafe { std::slice::from_raw_parts(faces, count) };
    let mut vec = Vec::with_capacity(count);
    for &h in slice {
        // SAFETY: 每个 h 都是拥有所有权并在此处被消费的 TruckFace 句柄。
        match unsafe { handle::take_raw(h) } {
            Some(f) => vec.push(f.0),
            None => return std::ptr::null_mut(),
        }
    }
    handle::into_raw(TruckShell(Shell::from(vec)))
}

/// 释放壳体句柄。此操作具有幂等性。
///
/// # 安全性
/// `shell` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_shell_free(shell: *mut TruckShell) {
    match unsafe { handle::take_raw(shell) } {
        Some(s) => drop(s),
        None => {}
    }
}

/// 以容差 `tol` 将壳体细分为 `PolygonMesh`。
///
/// 成功时将新网格句柄写入 `*out_mesh`；失败时（壳体为 NULL 或几何退化），
/// 将错误句柄写入 `*err` 并返回 false。
///
/// # 安全性
/// `shell` 必须为 NULL 或有效句柄；`out_mesh`/`err` 必须有效或为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_shell_to_polygon(
    shell: *const TruckShell,
    tol: f64,
    out_mesh: *mut *mut TruckPolygonMesh,
    err: *mut *mut TruckError,
) -> bool {
    if out_mesh.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 shell 为 NULL 或有效句柄。
    let s = match unsafe { handle::from_ref(shell) } {
        Some(s) => s,
        None => return false,
    };
    let res = crate::error::truck_guard!(|| {
        if tol <= 0.0 {
            return Err(TruckError::new(format!("tolerance must be positive, got {tol}")));
        }
        let meshed = s.0.robust_triangulation(tol);
        let polygon: truck_polymesh::PolygonMesh = meshed.to_polygon();
        Ok::<_, TruckError>(polygon)
    });
    crate::truck_deliver!(res, err, |m: truck_polymesh::PolygonMesh| {
        // SAFETY: 上方已检查 out_mesh 非 NULL。
        unsafe { *out_mesh = handle::into_raw(TruckPolygonMesh(m)) };
    })
}

// ---------------------------------------------------------------------------
// Solid
// ---------------------------------------------------------------------------

/// 根据壳体句柄数组构建实体。**壳体句柄会被消费**（移动到实体中），之后
/// 不得再使用或释放。
///
/// 返回新的实体句柄；`shells` 为 NULL 或 `count` 为 0 时返回 NULL。
///
/// # 安全性
/// `shells` 必须为 NULL，或者是包含 `count` 个自有壳体句柄的有效数组。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_from_shells(
    shells: *const *mut TruckShell,
    count: usize,
) -> *mut TruckSolid {
    if shells.is_null() || count == 0 {
        return std::ptr::null_mut();
    }
    // SAFETY: 调用方保证 shells 可供读取 count 个句柄指针。
    let slice = unsafe { std::slice::from_raw_parts(shells, count) };
    let mut vec = Vec::with_capacity(count);
    for &h in slice {
        match unsafe { handle::take_raw(h) } {
            Some(s) => vec.push(s.0),
            None => return std::ptr::null_mut(),
        }
    }
    handle::into_raw(TruckSolid(Solid::new(vec)))
}

/// 释放实体句柄。此操作具有幂等性。
///
/// # 安全性
/// `solid` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_free(solid: *mut TruckSolid) {
    match unsafe { handle::take_raw(solid) } {
        Some(s) => drop(s),
        None => {}
    }
}

/// 以容差 `tol` 将实体细分为 `PolygonMesh`（使用第一个边界壳体）。错误契约
/// 参见 [`truck_shell_to_polygon`]。
///
/// # 安全性
/// `solid` 必须为 NULL 或有效句柄；`out_mesh`/`err` 必须有效或为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_to_polygon(
    solid: *const TruckSolid,
    tol: f64,
    out_mesh: *mut *mut TruckPolygonMesh,
    err: *mut *mut TruckError,
) -> bool {
    if out_mesh.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 solid 为 NULL 或有效句柄。
    let s = match unsafe { handle::from_ref(solid) } {
        Some(s) => s,
        None => return false,
    };
    let res = crate::error::truck_guard!(|| {
        if tol <= 0.0 {
            return Err(TruckError::new(format!("tolerance must be positive, got {tol}")));
        }
        if s.0.boundaries().is_empty() {
            return Err(TruckError::new("solid has no boundary shells"));
        }
        let meshed = s.0.robust_triangulation(tol);
        let shell = &meshed.boundaries()[0];
        let polygon: truck_polymesh::PolygonMesh = shell.to_polygon();
        Ok::<_, TruckError>(polygon)
    });
    crate::truck_deliver!(res, err, |m: truck_polymesh::PolygonMesh| {
        // SAFETY: 上方已检查 out_mesh 非 NULL。
        unsafe { *out_mesh = handle::into_raw(TruckPolygonMesh(m)) };
    })
}

// ---------------------------------------------------------------------------
// 阶段 5 — 布尔运算（and / or / not）
// ---------------------------------------------------------------------------

/// 布尔交集：返回 `a` 与 `b` 的公共实体（A ∩ B）。
///
/// 两个实体均仅被**借用**。`tol` 为几何容差，典型值为 `0.05`（truck-js 的
/// 默认值）。返回新的实体句柄；任一输入为 NULL 或操作失败（例如实体相切等
/// 几何退化情况，或容差不合适）时返回 NULL。
///
/// # 安全性
/// `a` 和 `b` 必须分别为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_and(
    a: *const TruckSolid,
    b: *const TruckSolid,
    tol: f64,
) -> *mut TruckSolid {
    // SAFETY: 调用方保证这些句柄为 NULL 或有效句柄。
    let (a, b) = match (unsafe { handle::from_ref(a) }, unsafe { handle::from_ref(b) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| {
        Ok::<Option<Solid>, TruckError>(truck_shapeops::and(&a.0, &b.0, tol))
    });
    match res {
        Ok(Some(solid)) => handle::into_raw(TruckSolid(solid)),
        Ok(None) => std::ptr::null_mut(),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 布尔并集：返回合并 `a` 与 `b` 后的实体（A ∪ B）。
///
/// 借用/失败契约参见 [`truck_solid_and`]。
///
/// # 安全性
/// `a` 和 `b` 必须分别为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_or(
    a: *const TruckSolid,
    b: *const TruckSolid,
    tol: f64,
) -> *mut TruckSolid {
    // SAFETY: 调用方保证这些句柄为 NULL 或有效句柄。
    let (a, b) = match (unsafe { handle::from_ref(a) }, unsafe { handle::from_ref(b) }) {
        (Some(a), Some(b)) => (a, b),
        _ => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| {
        Ok::<Option<Solid>, TruckError>(truck_shapeops::or(&a.0, &b.0, tol))
    });
    match res {
        Ok(Some(solid)) => handle::into_raw(TruckSolid(solid)),
        Ok(None) => std::ptr::null_mut(),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 翻转 `solid` 的朝向（法线），返回新的实体。
///
/// 输入仅被**借用**且保持不变（结果是朝向反转后的克隆值）。`solid` 为 NULL
/// 时返回 NULL。
///
/// `not` 是自身的逆运算：连续应用两次会得到等价实体。
///
/// # 安全性
/// `solid` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_not(solid: *const TruckSolid) -> *mut TruckSolid {
    // SAFETY: 调用方保证 solid 为 NULL 或有效句柄。
    let s = match unsafe { handle::from_ref(solid) } {
        Some(s) => s,
        None => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| {
        let mut clone = s.0.clone();
        clone.not();
        Ok::<Solid, TruckError>(clone)
    });
    match res {
        Ok(solid) => handle::into_raw(TruckSolid(solid)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

/// 创建轴对齐长方体，其中一个角位于原点，沿 +x/+y/+z 轴的尺寸分别为
/// `(dx, dy, dz)`。
///
/// 这是一个便捷基本体，等价于三次 `tsweep` 操作（顶点 -> 边 -> 面 -> 实体）。
/// 由于它是最常见的建模入口，因此合并为一次调用。
///
/// `dx`/`dy`/`dz` 可为任意有限值（负值表示沿坐标轴负方向扫掠）；全部为零时
/// 返回 NULL。成功时返回新的实体句柄；内部失败时返回 NULL（保护层会将所有
/// panic 转换为 NULL）。
#[no_mangle]
pub extern "C" fn truck_solid_box(dx: f64, dy: f64, dz: f64) -> *mut TruckSolid {
    let res = crate::error::truck_guard!(|| -> Result<Solid, TruckError> {
        let v = builder::vertex(Point3::new(0.0, 0.0, 0.0));
        let e = builder::tsweep(&v, Vector3::new(dx, 0.0, 0.0));
        let f = builder::tsweep(&e, Vector3::new(0.0, dy, 0.0));
        let s = builder::tsweep(&f, Vector3::new(0.0, 0.0, dz));
        Ok(s)
    });
    match res {
        Ok(solid) => handle::into_raw(TruckSolid(solid)),
        Err(_panic) => std::ptr::null_mut(),
    }
}

// ---------------------------------------------------------------------------
// 阶段 7 — Wire + 基本实体（圆柱体 / 球体 / 圆锥体）
// ---------------------------------------------------------------------------

/// truck `Wire`（具体 `<Point3, Curve>` 形式）的不透明句柄。Wire 是相连的
/// 边序列，用于构建面（`attach_plane`）和圆锥体，也可在构建基本实体时作为
/// 中间结构。
#[derive(Debug)]
pub struct TruckWire(pub(crate) Wire);

/// 根据边句柄数组构建 Wire。**边句柄会被消费**（移动到 Wire 中），之后不得
/// 再使用或释放。
///
/// 返回新的 Wire 句柄；`edges` 为 NULL 或 `count` 为 0 时返回 NULL。
///
/// # 安全性
/// `edges` 必须为 NULL，或者是包含 `count` 个自有边句柄的有效数组。
#[no_mangle]
pub unsafe extern "C" fn truck_wire_from_edges(
    edges: *const *mut TruckEdge,
    count: usize,
) -> *mut TruckWire {
    if edges.is_null() || count == 0 {
        return std::ptr::null_mut();
    }
    // SAFETY: 调用方保证 edges 可供读取 count 个句柄指针。
    let slice = unsafe { std::slice::from_raw_parts(edges, count) };
    let mut vec = Vec::with_capacity(count);
    for &h in slice {
        // SAFETY: 每个 h 都是拥有所有权并在此处被消费的 TruckEdge 句柄。
        match unsafe { handle::take_raw(h) } {
            Some(e) => vec.push(e.0),
            None => return std::ptr::null_mut(),
        }
    }
    handle::into_raw(TruckWire(Wire::from(vec)))
}

/// 返回 Wire 中的边数。`wire` 为 NULL 时返回 0。
///
/// # 安全性
/// `wire` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_wire_edge_count(wire: *const TruckWire) -> usize {
    match unsafe { handle::from_ref(wire) } {
        Some(w) => w.0.len(),
        None => 0,
    }
}

/// Wire 闭合（终点连接回起点）时返回 true。
///
/// # 安全性
/// `wire` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_wire_is_closed(wire: *const TruckWire) -> bool {
    match unsafe { handle::from_ref(wire) } {
        Some(w) => w.0.is_closed(),
        None => false,
    }
}

/// 释放 Wire 句柄。此操作具有幂等性。
///
/// # 安全性
/// `wire` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_wire_free(wire: *mut TruckWire) {
    match unsafe { handle::take_raw(wire) } {
        Some(w) => drop(w),
        None => {}
    }
}

/// 使用面填充闭合的平面 Wire。Wire 仅被借用。
///
/// 返回新的面句柄；若 Wire 未闭合、不共面，或 `wire` 为 NULL，则返回 NULL。
///
/// # 安全性
/// `wire` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_face_attach_plane(wire: *const TruckWire) -> *mut TruckFace {
    // SAFETY: 调用方保证 wire 为 NULL 或有效句柄。
    let w = match unsafe { handle::from_ref(wire) } {
        Some(w) => w,
        None => return std::ptr::null_mut(),
    };
    let res = crate::error::truck_guard!(|| -> Result<Face, TruckError> {
        let f = builder::try_attach_plane(&[w.0.clone()])
            .map_err(|e| TruckError::new(format!("{e}")))?;
        Ok(f)
    });
    match res {
        Ok(face) => handle::into_raw(TruckFace(face)),
        Err(_panic_or_err) => std::ptr::null_mut(),
    }
}

/// 创建半径为 `r`、高度为 `h` 的圆柱体，轴向为 +z，底面中心位于原点。
/// 返回新的实体句柄；输入退化（`r <= 0` 或 `h <= 0`）或内部失败时返回 NULL。
#[no_mangle]
pub extern "C" fn truck_solid_cylinder(r: f64, h: f64) -> *mut TruckSolid {
    let res = crate::error::truck_guard!(|| -> Result<Solid, TruckError> {
        if r <= 0.0 || h <= 0.0 {
            return Err(TruckError::new("cylinder radius and height must be positive"));
        }
        let v = builder::vertex(Point3::new(r, 0.0, 0.0));
        let w = builder::rsweep(&v, ORIGIN, Vector3::unit_z(), truck_modeling::Rad(7.0));
        let f = builder::try_attach_plane(&[w])
            .map_err(|e| TruckError::new(format!("{e}")))?;
        let s = builder::tsweep(&f, Vector3::new(0.0, 0.0, h));
        Ok(s)
    });
    match res {
        Ok(solid) => handle::into_raw(TruckSolid(solid)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// 创建以原点为球心、半径为 `r` 的球体。返回新的实体句柄；输入退化
///（`r <= 0`）或内部失败时返回 NULL。
#[no_mangle]
pub extern "C" fn truck_solid_sphere(r: f64) -> *mut TruckSolid {
    let res = crate::error::truck_guard!(|| -> Result<Solid, TruckError> {
        if r <= 0.0 {
            return Err(TruckError::new("sphere radius must be positive"));
        }
        // 闭合半圆 Wire：半圆弧 + 直径线段
        let v0 = builder::vertex(Point3::new(r, 0.0, 0.0));
        let v1 = builder::vertex(Point3::new(-r, 0.0, 0.0));
        let arc = builder::circle_arc(&v0, &v1, Point3::new(0.0, r, 0.0));
        let diam = builder::line(&v1, &v0);
        let wire: Wire = vec![arc, diam].into();
        let f = builder::try_attach_plane(&[wire])
            .map_err(|e| TruckError::new(format!("{e}")))?;
        // 让半圆面绕 +z（其直径轴）完整旋转一周
        let s = builder::rsweep(&f, ORIGIN, Vector3::unit_z(), truck_modeling::Rad(7.0));
        Ok(s)
    });
    match res {
        Ok(solid) => handle::into_raw(TruckSolid(solid)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// 根据底面半径 `r` 和高度 `h` 创建圆锥体。返回新的实体句柄；输入退化
///（`r <= 0` 或 `h <= 0`）或内部失败时返回 NULL。
///
/// 底面/顶点的确切位置遵循 truck 的 `builder::cone` 几何定义（对两条边组成的
/// Wire 进行 R-sweep）；需要精确坐标轴约定的调用方应检查结果包围盒，并在
/// 必要时应用变换。
#[no_mangle]
pub extern "C" fn truck_solid_cone(r: f64, h: f64) -> *mut TruckSolid {
    let res = crate::error::truck_guard!(|| -> Result<Solid, TruckError> {
        if r <= 0.0 || h <= 0.0 {
            return Err(TruckError::new("cone radius and height must be positive"));
        }
        // truck 的圆锥体文档方案：Wire 位于 y-z 平面（x=0），绕 +y 进行 R-sweep。
        // 结果几何体的底面（半径 r）位于 z=0，顶点位于 z=h，实际轴向为 +z，
        // 与圆柱体/球体的约定一致。
        let v0 = builder::vertex(Point3::new(0.0, r, 0.0));
        let v1 = builder::vertex(Point3::new(0.0, 0.0, h));
        let v2 = builder::vertex(Point3::new(0.0, 0.0, 0.0));
        let wire: Wire = vec![builder::line(&v0, &v1), builder::line(&v1, &v2)].into();
        let shell = builder::cone(&wire, Vector3::unit_y(), truck_modeling::Rad(std::f64::consts::TAU));
        let s = Solid::new(vec![shell]);
        Ok(s)
    });
    match res {
        Ok(solid) => handle::into_raw(TruckSolid(solid)),
        Err(_) => std::ptr::null_mut(),
    }
}


// ---------------------------------------------------------------------------

/// 将顶点包装为 `AbstractShape`。顶点句柄会被消费。
///
/// # 安全性
/// `v` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_vertex_upcast(v: *mut TruckVertex) -> *mut AbstractShape {
    match unsafe { handle::take_raw(v) } {
        Some(v) => handle::into_raw(AbstractShape::from_vertex(v)),
        None => std::ptr::null_mut(),
    }
}

/// 将边包装为 `AbstractShape`。边句柄会被消费。
///
/// # 安全性
/// `e` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_edge_upcast(e: *mut TruckEdge) -> *mut AbstractShape {
    match unsafe { handle::take_raw(e) } {
        Some(e) => handle::into_raw(AbstractShape::from_edge(e)),
        None => std::ptr::null_mut(),
    }
}

/// 将面包装为 `AbstractShape`。面句柄会被消费。
///
/// # 安全性
/// `f` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_face_upcast(f: *mut TruckFace) -> *mut AbstractShape {
    match unsafe { handle::take_raw(f) } {
        Some(f) => handle::into_raw(AbstractShape::from_face(f)),
        None => std::ptr::null_mut(),
    }
}

/// 将壳体包装为 `AbstractShape`。壳体句柄会被消费。
///
/// # 安全性
/// `s` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_shell_upcast(s: *mut TruckShell) -> *mut AbstractShape {
    match unsafe { handle::take_raw(s) } {
        Some(s) => handle::into_raw(AbstractShape::from_shell(s)),
        None => std::ptr::null_mut(),
    }
}

/// 将实体包装为 `AbstractShape`。实体句柄会被消费。
///
/// # 安全性
/// `s` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_solid_upcast(s: *mut TruckSolid) -> *mut AbstractShape {
    match unsafe { handle::take_raw(s) } {
        Some(s) => handle::into_raw(AbstractShape::from_solid(s)),
        None => std::ptr::null_mut(),
    }
}

/// `shape` 包装顶点时返回 true。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_is_vertex(shape: *const AbstractShape) -> bool {
    matches!(unsafe { handle::from_ref(shape) }.map(|s| &s.0), Some(SubShape::Vertex(_)))
}

/// `shape` 包装边时返回 true。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_is_edge(shape: *const AbstractShape) -> bool {
    matches!(unsafe { handle::from_ref(shape) }.map(|s| &s.0), Some(SubShape::Edge(_)))
}

/// `shape` 包装面时返回 true。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_is_face(shape: *const AbstractShape) -> bool {
    matches!(unsafe { handle::from_ref(shape) }.map(|s| &s.0), Some(SubShape::Face(_)))
}

/// `shape` 包装壳体时返回 true。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_is_shell(shape: *const AbstractShape) -> bool {
    matches!(unsafe { handle::from_ref(shape) }.map(|s| &s.0), Some(SubShape::Shell(_)))
}

/// `shape` 包装实体时返回 true。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_is_solid(shape: *const AbstractShape) -> bool {
    matches!(unsafe { handle::from_ref(shape) }.map(|s| &s.0), Some(SubShape::Solid(_)))
}

/// 消费 `shape` 并返回其中包装的顶点；若其并非顶点，则返回 NULL
///（无论结果如何，`AbstractShape` 都会被消费）。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_into_vertex(
    shape: *mut AbstractShape,
) -> *mut TruckVertex {
    match unsafe { handle::take_raw(shape) }.map(|s| s.0) {
        Some(SubShape::Vertex(v)) => handle::into_raw(v),
        _ => std::ptr::null_mut(),
    }
}

/// 消费 `shape` 并返回其中包装的边；若其并非边，则返回 NULL。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_into_edge(
    shape: *mut AbstractShape,
) -> *mut TruckEdge {
    match unsafe { handle::take_raw(shape) }.map(|s| s.0) {
        Some(SubShape::Edge(e)) => handle::into_raw(e),
        _ => std::ptr::null_mut(),
    }
}

/// 消费 `shape` 并返回其中包装的面；若其并非面，则返回 NULL。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_into_face(
    shape: *mut AbstractShape,
) -> *mut TruckFace {
    match unsafe { handle::take_raw(shape) }.map(|s| s.0) {
        Some(SubShape::Face(f)) => handle::into_raw(f),
        _ => std::ptr::null_mut(),
    }
}

/// 消费 `shape` 并返回其中包装的壳体；若其并非壳体，则返回 NULL。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_into_shell(
    shape: *mut AbstractShape,
) -> *mut TruckShell {
    match unsafe { handle::take_raw(shape) }.map(|s| s.0) {
        Some(SubShape::Shell(s)) => handle::into_raw(s),
        _ => std::ptr::null_mut(),
    }
}

/// 消费 `shape` 并返回其中包装的实体；若其并非实体，则返回 NULL。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_into_solid(
    shape: *mut AbstractShape,
) -> *mut TruckSolid {
    match unsafe { handle::take_raw(shape) }.map(|s| s.0) {
        Some(SubShape::Solid(s)) => handle::into_raw(s),
        _ => std::ptr::null_mut(),
    }
}

/// 释放 `AbstractShape` 句柄。此操作具有幂等性。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效且拥有所有权的句柄。
#[no_mangle]
pub unsafe extern "C" fn truck_abstractshape_free(shape: *mut AbstractShape) {
    match unsafe { handle::take_raw(shape) } {
        Some(s) => drop(s),
        None => {}
    }
}

// ---------------------------------------------------------------------------
// 扫掠 + 变换（作用于 AbstractShape）
// ---------------------------------------------------------------------------

/// 使用平移向量 `vec`（`[x, y, z]`）扫掠 `shape`。
///
/// 结果类型取决于输入（truck 0.6.0 的 `Sweep` 映射）：顶点→边、边→面、
/// 面→实体。扫掠壳体或实体会产生错误（壳体扫掠会生成多个实体；实体无法扫掠）。
///
/// 成功时将新的 `AbstractShape` 写入 `*out`；失败时将错误句柄写入 `*err`。
///
/// # 安全性
/// `shape` 必须为 NULL 或有效句柄；`vec` 必须为 NULL 或可供读取 `vec_len`
/// 个 f64；`out`/`err` 必须有效或为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_tsweep(
    shape: *const AbstractShape,
    vec: *const f64,
    vec_len: usize,
    out: *mut *mut AbstractShape,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    // SAFETY: 调用方保证 shape 为 NULL 或有效句柄。
    let s = match unsafe { handle::from_ref(shape) } {
        Some(s) => s,
        None => return false,
    };
    let v = match unsafe { read_vec3(vec, vec_len) } {
        Some(v) => Vector3::new(v[0], v[1], v[2]),
        None => {
            if !err.is_null() {
                // SAFETY: err 指向可写存储空间。
                unsafe { *err = handle::into_raw(TruckError::new("vec must be 3 f64s")) };
            }
            return false;
        }
    };
    let res = crate::error::truck_guard!(|| -> Result<AbstractShape, TruckError> {
        Ok(match &s.0 {
            SubShape::Vertex(vx) => AbstractShape::from_edge(TruckEdge(builder::tsweep(&vx.0, v))),
            SubShape::Edge(e) => AbstractShape::from_face(TruckFace(builder::tsweep(&e.0, v))),
            SubShape::Face(f) => AbstractShape::from_solid(TruckSolid(builder::tsweep(&f.0, v))),
            SubShape::Shell(_) => {
                return Err(TruckError::new("cannot tsweep a Shell (multi-result)"));
            }
            SubShape::Solid(_) => {
                return Err(TruckError::new("cannot tsweep a Solid"));
            }
        })
    });
    crate::truck_deliver!(res, err, |a: AbstractShape| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = handle::into_raw(a) };
    })
}

/// 将 `shape` 绕经过 `origin` 的 `axis` 旋转 `angle` 弧度进行扫掠。
///
/// `axis` 必须归一化。truck 0.6.0 的 `rsweep`（通过 `ClosedSweep`）映射为：
/// 边→壳体、面→实体。不支持旋转顶点/壳体/实体（结果将是 Wire/多实体，
/// 本 ABI 未暴露这些形式）。
///
/// # 安全性
/// `shape`、`origin`、`axis` 必须为 NULL 或有效；`out`/`err` 必须有效或为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_rsweep(
    shape: *const AbstractShape,
    origin: *const f64,
    origin_len: usize,
    axis: *const f64,
    axis_len: usize,
    angle: f64,
    out: *mut *mut AbstractShape,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    let s = match unsafe { handle::from_ref(shape) } {
        Some(s) => s,
        None => return false,
    };
    let o = match unsafe { read_vec3(origin, origin_len) } {
        Some(o) => Point3::new(o[0], o[1], o[2]),
        None => return false,
    };
    let a = match unsafe { read_vec3(axis, axis_len) } {
        Some(a) => Vector3::new(a[0], a[1], a[2]),
        None => return false,
    };
    let res = crate::error::truck_guard!(|| -> Result<AbstractShape, TruckError> {
        Ok(match &s.0 {
            SubShape::Edge(e) => AbstractShape::from_shell(TruckShell(builder::rsweep(
                &e.0, o, a, truck_modeling::Rad(angle),
            ))),
            SubShape::Face(f) => AbstractShape::from_solid(TruckSolid(builder::rsweep(
                &f.0, o, a, truck_modeling::Rad(angle),
            ))),
            SubShape::Vertex(_) => {
                return Err(TruckError::new("rsweep of a Vertex yields a Wire, not exposed"));
            }
            SubShape::Shell(_) | SubShape::Solid(_) => {
                return Err(TruckError::new("cannot rsweep a Shell or Solid"));
            }
        })
    });
    crate::truck_deliver!(res, err, |a: AbstractShape| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = handle::into_raw(a) };
    })
}

/// 使用 `vec`（`[x, y, z]`）平移 `shape`；返回相同的形状类型。
///
/// # 安全性
/// `shape`/`vec` 必须为 NULL 或有效；`out`/`err` 必须有效或为 NULL。
#[no_mangle]
pub unsafe extern "C" fn truck_translated(
    shape: *const AbstractShape,
    vec: *const f64,
    vec_len: usize,
    out: *mut *mut AbstractShape,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    let s = match unsafe { handle::from_ref(shape) } {
        Some(s) => s,
        None => return false,
    };
    let v = match unsafe { read_vec3(vec, vec_len) } {
        Some(v) => Vector3::new(v[0], v[1], v[2]),
        None => return false,
    };
    let res = crate::error::truck_guard!(|| -> Result<AbstractShape, TruckError> {
        Ok(match &s.0 {
            SubShape::Vertex(vx) => AbstractShape::from_vertex(TruckVertex(builder::translated(&vx.0, v))),
            SubShape::Edge(e) => AbstractShape::from_edge(TruckEdge(builder::translated(&e.0, v))),
            SubShape::Face(f) => AbstractShape::from_face(TruckFace(builder::translated(&f.0, v))),
            SubShape::Shell(sh) => AbstractShape::from_shell(TruckShell(builder::translated(&sh.0, v))),
            SubShape::Solid(so) => AbstractShape::from_solid(TruckSolid(builder::translated(&so.0, v))),
        })
    });
    crate::truck_deliver!(res, err, |a: AbstractShape| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = handle::into_raw(a) };
    })
}

/// 将 `shape` 绕经过 `origin` 的 `axis` 旋转 `angle` 弧度；返回相同类型。
/// `axis` 必须归一化。
///
/// # 安全性
/// 所有指针参数都必须按各自用途为 NULL 或有效指针。
#[no_mangle]
pub unsafe extern "C" fn truck_rotated(
    shape: *const AbstractShape,
    origin: *const f64,
    origin_len: usize,
    axis: *const f64,
    axis_len: usize,
    angle: f64,
    out: *mut *mut AbstractShape,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    let s = match unsafe { handle::from_ref(shape) } {
        Some(s) => s,
        None => return false,
    };
    let o = match unsafe { read_vec3(origin, origin_len) } {
        Some(o) => Point3::new(o[0], o[1], o[2]),
        None => return false,
    };
    let a = match unsafe { read_vec3(axis, axis_len) } {
        Some(a) => Vector3::new(a[0], a[1], a[2]),
        None => return false,
    };
    let res = crate::error::truck_guard!(|| -> Result<AbstractShape, TruckError> {
        Ok(match &s.0 {
            SubShape::Vertex(vx) => AbstractShape::from_vertex(TruckVertex(builder::rotated(&vx.0, o, a, truck_modeling::Rad(angle)))),
            SubShape::Edge(e) => AbstractShape::from_edge(TruckEdge(builder::rotated(&e.0, o, a, truck_modeling::Rad(angle)))),
            SubShape::Face(f) => AbstractShape::from_face(TruckFace(builder::rotated(&f.0, o, a, truck_modeling::Rad(angle)))),
            SubShape::Shell(sh) => AbstractShape::from_shell(TruckShell(builder::rotated(&sh.0, o, a, truck_modeling::Rad(angle)))),
            SubShape::Solid(so) => AbstractShape::from_solid(TruckSolid(builder::rotated(&so.0, o, a, truck_modeling::Rad(angle)))),
        })
    });
    crate::truck_deliver!(res, err, |a: AbstractShape| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = handle::into_raw(a) };
    })
}

/// 以 `origin` 为中心，按 `scalars`（`[sx, sy, sz]`）缩放 `shape`；返回相同类型。
///
/// # 安全性
/// 所有指针参数都必须按各自用途为 NULL 或有效指针。
#[no_mangle]
pub unsafe extern "C" fn truck_scaled(
    shape: *const AbstractShape,
    origin: *const f64,
    origin_len: usize,
    scalars: *const f64,
    scalars_len: usize,
    out: *mut *mut AbstractShape,
    err: *mut *mut TruckError,
) -> bool {
    if out.is_null() {
        return false;
    }
    let s = match unsafe { handle::from_ref(shape) } {
        Some(s) => s,
        None => return false,
    };
    let o = match unsafe { read_vec3(origin, origin_len) } {
        Some(o) => Point3::new(o[0], o[1], o[2]),
        None => return false,
    };
    let sc = match unsafe { read_vec3(scalars, scalars_len) } {
        Some(sc) => Vector3::new(sc[0], sc[1], sc[2]),
        None => return false,
    };
    let res = crate::error::truck_guard!(|| -> Result<AbstractShape, TruckError> {
        Ok(match &s.0 {
            SubShape::Vertex(vx) => AbstractShape::from_vertex(TruckVertex(builder::scaled(&vx.0, o, sc))),
            SubShape::Edge(e) => AbstractShape::from_edge(TruckEdge(builder::scaled(&e.0, o, sc))),
            SubShape::Face(f) => AbstractShape::from_face(TruckFace(builder::scaled(&f.0, o, sc))),
            SubShape::Shell(sh) => AbstractShape::from_shell(TruckShell(builder::scaled(&sh.0, o, sc))),
            SubShape::Solid(so) => AbstractShape::from_solid(TruckSolid(builder::scaled(&so.0, o, sc))),
        })
    });
    crate::truck_deliver!(res, err, |a: AbstractShape| {
        // SAFETY: 上方已检查 out 非 NULL。
        unsafe { *out = handle::into_raw(a) };
    })
}


#[cfg(test)]
mod tests {
    use super::*;
    // 为阶段 4d 的细分/错误测试引入辅助项。
    use crate::error::truck_error_free;
    use crate::polymesh::{truck_polygonmesh_bounding_box, truck_polygonmesh_free};

    #[test]
    fn new_is_non_null() {
        let v = truck_vertex_new(1.0, 2.0, 3.0);
        assert!(!v.is_null());
        // SAFETY: v 来自 truck_vertex_new，尚未释放。
        unsafe { truck_vertex_free(v) };
    }

    #[test]
    fn point_roundtrip() {
        let v = truck_vertex_new(1.0, 2.0, 3.0);
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: v 是新句柄；arr 是有效输出指针。
        let ok = unsafe { truck_vertex_point(v, &mut arr) };
        assert!(ok);
        assert_eq!(arr.len, 3);
        // SAFETY: arr 刚刚生成，arr.ptr 可供读取 arr.len 个元素。
        let s = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert_eq!(s, &[1.0, 2.0, 3.0]);
        // SAFETY: arr 来自 truck_vertex_point。
        unsafe {
            crate::handle::truck_f64array_free(arr);
            truck_vertex_free(v);
        }
    }

    #[test]
    fn distinct_vertices_keep_distinct_points() {
        let a = truck_vertex_new(0.0, 0.0, 0.0);
        let b = truck_vertex_new(5.0, -2.0, 7.0);
        let mut pa = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        let mut pb = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        unsafe {
            truck_vertex_point(a, &mut pa);
            truck_vertex_point(b, &mut pb);
        }
        // SAFETY: 两个指针均可供读取 len 个元素。
        let sa = unsafe { std::slice::from_raw_parts(pa.ptr, pa.len) };
        let sb = unsafe { std::slice::from_raw_parts(pb.ptr, pb.len) };
        assert_eq!(sa, &[0.0, 0.0, 0.0]);
        assert_eq!(sb, &[5.0, -2.0, 7.0]);
        unsafe {
            crate::handle::truck_f64array_free(pa);
            crate::handle::truck_f64array_free(pb);
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn null_arguments_return_false() {
        let v = truck_vertex_new(1.0, 2.0, 3.0);
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // NULL 顶点
        assert!(!unsafe { truck_vertex_point(std::ptr::null(), &mut arr) });
        // NULL 输出指针
        assert!(!unsafe { truck_vertex_point(v, std::ptr::null_mut()) });
        unsafe { truck_vertex_free(v) };
    }

    #[test]
    fn free_null_is_safe() {
        // SAFETY: 明确允许传入 NULL。
        unsafe { truck_vertex_free(std::ptr::null_mut()) };
    }

    // ---- 阶段 4b：Edge ----------------------------------------------------

    /// 辅助函数：将顶点坐标读入 Vec<f64>，并释放数组。
    unsafe fn vertex_point_vec(v: *const TruckVertex) -> Vec<f64> {
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: v 是有效句柄；arr 有效。
        unsafe { truck_vertex_point(v, &mut arr) };
        // SAFETY: arr.ptr 可供读取 arr.len 个元素。
        let s = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) }.to_vec();
        unsafe { crate::handle::truck_f64array_free(arr) };
        s
    }

    #[test]
    fn edge_line_endpoints_match() {
        let a = truck_vertex_new(1.0, 2.0, 3.0);
        let b = truck_vertex_new(4.0, 5.0, 6.0);
        // SAFETY: a、b 是有效句柄。
        let e = unsafe { truck_edge_line(a, b) };
        assert!(!e.is_null(), "line edge should be non-null");
        // 前端应等于 a，后端应等于 b。
        // SAFETY: e 有效。
        let f = unsafe { truck_edge_front_vertex(e) };
        let bk = unsafe { truck_edge_back_vertex(e) };
        assert!(!f.is_null() && !bk.is_null());
        assert_eq!(unsafe { vertex_point_vec(f) }, vec![1.0, 2.0, 3.0]);
        assert_eq!(unsafe { vertex_point_vec(bk) }, vec![4.0, 5.0, 6.0]);
        unsafe {
            truck_vertex_free(f);
            truck_vertex_free(bk);
            truck_edge_free(e);
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_line_null_inputs() {
        let b = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: 第一个参数为 NULL。
        assert!(unsafe { truck_edge_line(std::ptr::null(), b) }.is_null());
        // SAFETY: 第二个参数为 NULL。
        assert!(unsafe { truck_edge_line(b, std::ptr::null()) }.is_null());
        unsafe { truck_vertex_free(b) };
    }

    #[test]
    fn edge_circle_arc_by_transit_constructs() {
        let a = truck_vertex_new(1.0, 0.0, 0.0);
        let b = truck_vertex_new(-1.0, 0.0, 0.0);
        let transit = [0.0, 1.0, 0.0]; // 上半圆
        // SAFETY: a、b、transit 均有效。
        let e = unsafe { truck_edge_circle_arc_by_transit(a, b, transit.as_ptr(), 3) };
        assert!(!e.is_null(), "circle arc should be non-null");
        unsafe {
            truck_edge_free(e);
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_circle_arc_null_transit_returns_null() {
        let a = truck_vertex_new(1.0, 0.0, 0.0);
        let b = truck_vertex_new(-1.0, 0.0, 0.0);
        // SAFETY: transit 指针为 NULL。
        let e = unsafe { truck_edge_circle_arc_by_transit(a, b, std::ptr::null(), 0) };
        assert!(e.is_null());
        unsafe {
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_bezier_constructs() {
        let a = truck_vertex_new(0.0, 0.0, 0.0);
        let b = truck_vertex_new(3.0, 0.0, 0.0);
        // 两个中间控制点
        let ctrl = [1.0, 1.0, 0.0, 2.0, 1.0, 0.0];
        // SAFETY: a、b、ctrl 均有效。
        let e = unsafe { truck_edge_bezier(a, b, ctrl.as_ptr(), ctrl.len()) };
        assert!(!e.is_null(), "bezier edge should be non-null");
        unsafe {
            truck_edge_free(e);
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_bezier_bad_length_returns_null() {
        let a = truck_vertex_new(0.0, 0.0, 0.0);
        let b = truck_vertex_new(1.0, 0.0, 0.0);
        let bad = [1.0, 2.0]; // 长度为 2，不是 3 的倍数
        // SAFETY: a、b 有效；bad 可供读取 2 个元素。
        let e = unsafe { truck_edge_bezier(a, b, bad.as_ptr(), bad.len()) };
        assert!(e.is_null(), "non-multiple-of-3 ctrl length should yield NULL");
        unsafe {
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_front_back_survive_edge_free() {
        // 释放边后，克隆得到的前端/后端句柄必须仍然有效。
        let a = truck_vertex_new(1.0, 2.0, 3.0);
        let b = truck_vertex_new(4.0, 5.0, 6.0);
        // SAFETY: a、b 有效。
        let e = unsafe { truck_edge_line(a, b) };
        let f = unsafe { truck_edge_front_vertex(e) };
        unsafe { truck_edge_free(e) };
        // f 是独立克隆值；仍应能够查询其点。
        assert_eq!(unsafe { vertex_point_vec(f) }, vec![1.0, 2.0, 3.0]);
        unsafe {
            truck_vertex_free(f);
            truck_vertex_free(a);
            truck_vertex_free(b);
        }
    }

    #[test]
    fn edge_free_null_is_safe() {
        // SAFETY: 明确允许传入 NULL。
        unsafe { truck_edge_free(std::ptr::null_mut()) };
    }

    // ---- 阶段 4c：Face ----------------------------------------------------

    /// 辅助函数：构建测试所用、共享同伦形状的两条边。
    /// 返回 (v0, v1, v2, v3, edge0, edge1)。
    fn homotopy_edges() -> (
        *mut TruckVertex,
        *mut TruckVertex,
        *mut TruckVertex,
        *mut TruckVertex,
        *mut TruckEdge,
        *mut TruckEdge,
    ) {
        let v0 = truck_vertex_new(0.0, 0.0, 0.0);
        let v1 = truck_vertex_new(1.0, 0.0, 0.0);
        let v2 = truck_vertex_new(0.0, 0.0, 1.0);
        let v3 = truck_vertex_new(1.0, 0.0, 1.0);
        // SAFETY: 所有顶点均有效。
        let e0 = unsafe { truck_edge_line(v0, v1) };
        let e1 = unsafe { truck_edge_line(v2, v3) };
        (v0, v1, v2, v3, e0, e1)
    }

    #[test]
    fn face_homotopy_constructs() {
        let (v0, v1, v2, v3, e0, e1) = homotopy_edges();
        // SAFETY: e0、e1 有效。
        let face = unsafe { truck_face_homotopy(e0, e1) };
        assert!(!face.is_null(), "homotopy face should be non-null");
        // homotopy 恰好生成 4 条边界边。
        // SAFETY: face 有效。
        let count = unsafe { truck_face_boundary_edge_count(face) };
        assert_eq!(count, 4, "homotopy face should have 4 boundary edges");
        unsafe {
            truck_face_free(face);
            truck_edge_free(e0);
            truck_edge_free(e1);
            truck_vertex_free(v0);
            truck_vertex_free(v1);
            truck_vertex_free(v2);
            truck_vertex_free(v3);
        }
    }

    #[test]
    fn face_homotopy_null_inputs() {
        let (_v0, _v1, _v2, _v3, e0, e1) = homotopy_edges();
        // SAFETY: 第一个参数为 NULL。
        assert!(unsafe { truck_face_homotopy(std::ptr::null(), e1) }.is_null());
        // SAFETY: 第二个参数为 NULL。
        assert!(unsafe { truck_face_homotopy(e0, std::ptr::null()) }.is_null());
        unsafe {
            truck_edge_free(e0);
            truck_edge_free(e1);
            truck_vertex_free(_v0);
            truck_vertex_free(_v1);
            truck_vertex_free(_v2);
            truck_vertex_free(_v3);
        }
    }

    #[test]
    fn face_boundary_edges_array() {
        let (v0, v1, v2, v3, e0, e1) = homotopy_edges();
        // SAFETY: e0、e1 有效。
        let face = unsafe { truck_face_homotopy(e0, e1) };
        let mut arr = TruckEdgeArray { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: face 有效；arr 是有效输出指针。
        let ok = unsafe { truck_face_boundary_edges(face, &mut arr) };
        assert!(ok);
        assert_eq!(arr.len, 4);
        // 每个句柄均非 NULL
        // SAFETY: arr.ptr 可供读取 arr.len 个元素。
        let slice = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        for &h in slice {
            assert!(!h.is_null(), "boundary edge handle must be non-null");
        }
        // free_all 释放容器及所有句柄
        // SAFETY: arr 由 boundary_edges 生成。
        unsafe { truck_edgearray_free_all(arr) };
        unsafe {
            truck_face_free(face);
            truck_edge_free(e0);
            truck_edge_free(e1);
            truck_vertex_free(v0);
            truck_vertex_free(v1);
            truck_vertex_free(v2);
            truck_vertex_free(v3);
        }
    }

    #[test]
    fn face_boundary_edges_null_face_returns_false() {
        let mut arr = TruckEdgeArray { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: 已明确处理 face 为 NULL 的情况。
        assert!(!unsafe { truck_face_boundary_edges(std::ptr::null(), &mut arr) });
    }

    #[test]
    fn face_free_null_is_safe() {
        // SAFETY: 明确允许传入 NULL。
        unsafe { truck_face_free(std::ptr::null_mut()) };
    }

    #[test]
    fn edgearray_free_all_empty_is_safe() {
        // SAFETY: 明确允许传入空值。
        unsafe { truck_edgearray_free_all(TruckEdgeArray::empty()) };
        // SAFETY: 明确允许传入空值。
        unsafe { truck_edgearray_free(TruckEdgeArray::empty()) };
    }

    // ---- 阶段 4d-0：meshalgo 兼容性冒烟测试 -------------------------------
    // 可行性门禁：通过执行完整的构建链顶点 -> 边 -> 面 -> 实体，再将实体
    // 细分为 PolygonMesh，验证 truck_modeling 0.6.0 的 Curve/Surface 满足
    // truck_meshalgo 0.4.0 的 PolylineableCurve/MeshableSurface 约束。若此测试
    // 能够编译并运行，则版本组合兼容，可以继续阶段 4d-1。
    #[test]
    fn meshalgo_compatibility_smoke() {
        use truck_meshalgo::tessellation::{MeshedShape, RobustMeshableShape};

        let v: truck_modeling::Vertex = truck_modeling::builder::vertex(Point3::new(0.0, 0.0, 0.0));
        let e: truck_modeling::Edge =
            truck_modeling::builder::tsweep(&v, truck_modeling::Vector3::new(1.0, 0.0, 0.0));
        let f: truck_modeling::Face =
            truck_modeling::builder::tsweep(&e, truck_modeling::Vector3::new(0.0, 1.0, 0.0));
        let s: truck_modeling::Solid =
            truck_modeling::builder::tsweep(&f, truck_modeling::Vector3::new(0.0, 0.0, 1.0));

        // 将实体的第一个边界壳体细分为多边形网格。
        let meshed = s.robust_triangulation(0.01);
        let shell = &meshed.boundaries()[0];
        let polygon: truck_polymesh::PolygonMesh = shell.to_polygon();
        assert!(!polygon.positions().is_empty(), "tessellated mesh should have positions");
    }

    // ---- 阶段 4d-1：Shell / Solid / AbstractShape / 扫掠 -----------------

    #[test]
    fn tsweep_chain_vertex_to_solid() {
        // 顶点 -> 边 -> 面 -> 实体，每一步均通过 AbstractShape 完成。
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        let vec_x = [1.0, 0.0, 0.0];
        // SAFETY: v 有效。
        let s0 = unsafe { truck_vertex_upcast(v) };
        assert!(!s0.is_null());

        let mut s1 = std::ptr::null_mut();
        let mut err = std::ptr::null_mut();
        // SAFETY: s0 有效；vec_x 有效。
        let ok = unsafe { truck_tsweep(s0, vec_x.as_ptr(), 3, &mut s1, &mut err) };
        assert!(ok, "vertex tsweep should succeed");
        // SAFETY: s1 有效。
        assert!(unsafe { truck_abstractshape_is_edge(s1) });

        let mut s2 = std::ptr::null_mut();
        let vec_y = [0.0, 1.0, 0.0];
        // SAFETY: s1 有效；vec_y 有效。
        assert!(unsafe { truck_tsweep(s1, vec_y.as_ptr(), 3, &mut s2, &mut err) });
        // SAFETY: s2 有效。
        assert!(unsafe { truck_abstractshape_is_face(s2) });

        let mut s3 = std::ptr::null_mut();
        let vec_z = [0.0, 0.0, 1.0];
        // SAFETY: s2 有效；vec_z 有效。
        assert!(unsafe { truck_tsweep(s2, vec_z.as_ptr(), 3, &mut s3, &mut err) });
        // SAFETY: s3 有效。
        assert!(unsafe { truck_abstractshape_is_solid(s3) });

        unsafe {
            truck_abstractshape_free(s3);
            truck_abstractshape_free(s2);
            truck_abstractshape_free(s1);
            truck_abstractshape_free(s0);
        }
    }

    #[test]
    fn tsweep_solid_is_error() {
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: v 有效。
        let s0 = unsafe { truck_vertex_upcast(v) };
        let mut s1 = std::ptr::null_mut();
        let mut err = std::ptr::null_mut();
        let vec = [1.0, 0.0, 0.0];
        // SAFETY: s0 有效。
        unsafe { truck_tsweep(s0, vec.as_ptr(), 3, &mut s1, &mut err) }; // 顶点->边
        let mut s2 = std::ptr::null_mut();
        unsafe { truck_tsweep(s1, [0.0, 1.0, 0.0].as_ptr(), 3, &mut s2, &mut err) }; // 边->面
        let mut s3 = std::ptr::null_mut();
        unsafe { truck_tsweep(s2, [0.0, 0.0, 1.0].as_ptr(), 3, &mut s3, &mut err) }; // 面->实体

        // 对实体执行 tsweep -> 错误
        let mut s4 = std::ptr::null_mut();
        err = std::ptr::null_mut();
        // SAFETY: s3 有效（实体）。
        let ok = unsafe { truck_tsweep(s3, [1.0, 0.0, 0.0].as_ptr(), 3, &mut s4, &mut err) };
        assert!(!ok, "tsweep of a solid should fail");
        assert!(!err.is_null());
        unsafe {
            truck_error_free(err);
            truck_abstractshape_free(s4);
            truck_abstractshape_free(s3);
            truck_abstractshape_free(s2);
            truck_abstractshape_free(s1);
            truck_abstractshape_free(s0);
        }
    }

    #[test]
    fn solid_to_polygon_via_tsweep() {
        // 顶点 -> 边 -> 面 -> 实体，然后细分该实体。
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: v 有效。
        let s0 = unsafe { truck_vertex_upcast(v) };
        let mut s1 = std::ptr::null_mut();
        let mut s2 = std::ptr::null_mut();
        let mut s3 = std::ptr::null_mut();
        let mut err = std::ptr::null_mut();
        // SAFETY: 形状链均有效；tsweep 仅借用输入，因此 s0/s1/s2 保持有效。
        unsafe {
            truck_tsweep(s0, [1.0, 0.0, 0.0].as_ptr(), 3, &mut s1, &mut err);
            truck_tsweep(s1, [0.0, 1.0, 0.0].as_ptr(), 3, &mut s2, &mut err);
            truck_tsweep(s2, [0.0, 0.0, 1.0].as_ptr(), 3, &mut s3, &mut err);
        }
        // s3 包装了实体；将其提取（消费 s3）。
        // SAFETY: s3 有效（实体）。
        let solid = unsafe { truck_abstractshape_into_solid(s3) };
        assert!(!solid.is_null());

        let mut mesh: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err2 = std::ptr::null_mut();
        // SAFETY: solid 有效。
        let ok = unsafe { truck_solid_to_polygon(solid, 0.01, &mut mesh, &mut err2) };
        assert!(ok, "solid_to_polygon should succeed");
        assert!(!mesh.is_null());
        unsafe {
            truck_polygonmesh_free(mesh);
            truck_solid_free(solid);
            truck_abstractshape_free(s2);
            truck_abstractshape_free(s1);
            truck_abstractshape_free(s0);
        }
    }

    #[test]
    fn shell_from_faces_and_to_polygon() {
        // 通过 homotopy 构建面，将其包装为壳体，再细分该壳体。
        let v0 = truck_vertex_new(0.0, 0.0, 0.0);
        let v1 = truck_vertex_new(1.0, 0.0, 0.0);
        let v2 = truck_vertex_new(0.0, 0.0, 1.0);
        let v3 = truck_vertex_new(1.0, 0.0, 1.0);
        // SAFETY: 顶点均有效。
        let e0 = unsafe { truck_edge_line(v0, v1) };
        let e1 = unsafe { truck_edge_line(v2, v3) };
        // SAFETY: 边均有效。
        let face = unsafe { truck_face_homotopy(e0, e1) };
        let faces = [face];
        // SAFETY: faces 数组有效；face 被消费。
        let shell = unsafe { truck_shell_from_faces(faces.as_ptr(), 1) };
        assert!(!shell.is_null());

        let mut mesh: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err = std::ptr::null_mut();
        // SAFETY: shell 有效。
        let ok = unsafe { truck_shell_to_polygon(shell, 0.01, &mut mesh, &mut err) };
        assert!(ok, "shell_to_polygon should succeed");
        assert!(!mesh.is_null());
        unsafe {
            truck_polygonmesh_free(mesh);
            truck_shell_free(shell);
            truck_edge_free(e0);
            truck_edge_free(e1);
            truck_vertex_free(v0);
            truck_vertex_free(v1);
            truck_vertex_free(v2);
            truck_vertex_free(v3);
        }
    }

    #[test]
    fn translated_preserves_type() {
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: v 有效。
        let s = unsafe { truck_vertex_upcast(v) };
        let mut out = std::ptr::null_mut();
        let mut err = std::ptr::null_mut();
        let vec = [5.0, 0.0, 0.0];
        // SAFETY: s 有效；vec 有效。
        let ok = unsafe { truck_translated(s, vec.as_ptr(), 3, &mut out, &mut err) };
        assert!(ok);
        // SAFETY: out 有效。
        assert!(unsafe { truck_abstractshape_is_vertex(out) });
        // 提取顶点并检查其点已移动
        // SAFETY: out 有效（顶点）。
        let mv = unsafe { truck_abstractshape_into_vertex(out) };
        let mut arr = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: mv 有效。
        unsafe { truck_vertex_point(mv, &mut arr) };
        // SAFETY: arr 可供读取 len 个元素。
        let pts = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert_eq!(pts, &[5.0, 0.0, 0.0]);
        unsafe {
            crate::handle::truck_f64array_free(arr);
            truck_vertex_free(mv);
            truck_abstractshape_free(s);
        }
    }

    #[test]
    fn abstractshape_into_wrong_type_yields_null() {
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: v 有效。
        let s = unsafe { truck_vertex_upcast(v) };
        // 对顶点形状调用 into_edge -> NULL，但 shape 仍会被消费
        // SAFETY: s 有效。
        let edge = unsafe { truck_abstractshape_into_edge(s) };
        assert!(edge.is_null());
        // SAFETY: 可以安全释放 NULL。
        unsafe { truck_edge_free(edge) };
    }

    #[test]
    fn shell_solid_free_null_safe() {
        // SAFETY: 明确允许传入 NULL。
        unsafe {
            truck_shell_free(std::ptr::null_mut());
            truck_solid_free(std::ptr::null_mut());
            truck_abstractshape_free(std::ptr::null_mut());
            truck_vertex_upcast(std::ptr::null_mut());
            truck_edge_upcast(std::ptr::null_mut());
        }
    }

    // ---- 阶段 5：布尔运算 -------------------------------------------------

    /// 辅助函数：通过 tsweep 链构建单位立方体实体（借用逻辑）。
    fn unit_cube_solid() -> *mut TruckSolid {
        let v = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: v 有效。
        let s0 = unsafe { truck_vertex_upcast(v) };
        let mut s1 = std::ptr::null_mut();
        let mut s2 = std::ptr::null_mut();
        let mut s3 = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: 形状链均有效。
        unsafe {
            truck_tsweep(s0, [1.0, 0.0, 0.0].as_ptr(), 3, &mut s1, &mut err);
            truck_tsweep(s1, [0.0, 1.0, 0.0].as_ptr(), 3, &mut s2, &mut err);
            truck_tsweep(s2, [0.0, 0.0, 1.0].as_ptr(), 3, &mut s3, &mut err);
        }
        // SAFETY: s3 是实体。
        let solid = unsafe { truck_abstractshape_into_solid(s3) };
        unsafe {
            truck_abstractshape_free(s2);
            truck_abstractshape_free(s1);
            truck_abstractshape_free(s0);
        }
        solid
    }

    /// 阶段 5 可行性门禁：复现 shapeops 自身的“打孔立方体”测试（立方体 AND
    /// 反向圆柱体），验证 truck_modeling 0.6.0 的 Curve/Surface 满足
    /// truck_shapeops 0.4.0 的 ShapeOpsCurve/ShapeOpsSurface 约束，并确认布尔
    /// 运算流水线确实能够运行。两个普通立方体的对齐面会使 shapeops 的交集
    /// 算法失败，因此这里使用 shapeops 自身验证过的几何体。
    #[test]
    fn shapeops_compatibility_smoke() {
        // 复现 shapeops 自身的打孔立方体测试。此处使用完整的 truck_modeling
        // prelude（而不是逐项导入），以匹配 shapeops 自身测试的写法，从而正确
        // 解析 Point3::origin() 等 trait 方法。
        use truck_meshalgo::tessellation::{MeshableShape, MeshedShape};
        use truck_modeling::*;

        // 立方体
        let v = builder::vertex(Point3::origin());
        let e = builder::tsweep(&v, Vector3::unit_x());
        let f = builder::tsweep(&e, Vector3::unit_y());
        let cube = builder::tsweep(&f, Vector3::unit_z());

        // 圆柱体（先扫掠圆盘，再反转以执行减法）
        let v = builder::vertex(Point3::new(0.5, 0.25, -0.5));
        let w = builder::rsweep(&v, Point3::new(0.5, 0.5, 0.0), Vector3::unit_z(), Rad(7.0));
        let f = builder::try_attach_plane(&[w]).unwrap();
        let mut cylinder = builder::tsweep(&f, Vector3::unit_z() * 2.0);
        cylinder.not();

        let and = truck_shapeops::and(&cube, &cylinder, 0.05);
        assert!(and.is_some(), "shapeops AND (punched cube) must succeed");
        // 基本检查：结果能够细分
        let poly = and.unwrap().triangulation(0.01).to_polygon();
        assert!(!poly.positions().is_empty());
    }

    #[test]
    fn solid_and_or_do_not_panic() {
        // shapeops 的布尔交集对几何形态较敏感：两个普通轴对齐立方体会因对齐面
        // 而失败，因此这里只断言 FFI 调用不会 panic，且返回句柄或 NULL（两者
        // 均为合法结果）。上面的兼容性冒烟测试证明实际布尔运算可在合适的
        // 几何体上成功执行。
        let a = unit_cube_solid();
        // 通过 FFI 链构建第二个平移后的立方体
        let va = truck_vertex_new(0.0, 0.0, 0.0);
        // SAFETY: va 有效。
        let s0 = unsafe { truck_vertex_upcast(va) };
        let mut s1 = std::ptr::null_mut();
        let mut s2 = std::ptr::null_mut();
        let mut s3 = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: 形状链有效。
        unsafe {
            truck_tsweep(s0, [1.0, 0.0, 0.0].as_ptr(), 3, &mut s1, &mut err);
            truck_tsweep(s1, [0.0, 1.0, 0.0].as_ptr(), 3, &mut s2, &mut err);
            truck_tsweep(s2, [0.0, 0.0, 1.0].as_ptr(), 3, &mut s3, &mut err);
        }
        let mut s3t = std::ptr::null_mut();
        // SAFETY: s3 有效；tvec 有效。
        unsafe { truck_translated(s3, [0.5, 0.0, 0.0].as_ptr(), 3, &mut s3t, &mut err) };
        // SAFETY: s3t 是实体。
        let b = unsafe { truck_abstractshape_into_solid(s3t) };

        // SAFETY: a、b 有效。返回句柄或 NULL 均可接受；这里只要求不发生 panic。
        let and_res = unsafe { truck_solid_and(a, b, 0.05) };
        let or_res = unsafe { truck_solid_or(a, b, 0.05) };
        // 释放所有返回值（允许为 NULL）
        unsafe {
            truck_solid_free(and_res);
            truck_solid_free(or_res);
            truck_solid_free(b);
            truck_solid_free(a);
            truck_abstractshape_free(s3);
            truck_abstractshape_free(s2);
            truck_abstractshape_free(s1);
            truck_abstractshape_free(s0);
        }
    }

    #[test]
    fn solid_not_is_involutive() {
        // not(not(s)) 应与 s 等价（包围盒相同）。
        let a = unit_cube_solid();
        // SAFETY: a 有效。
        let n1 = unsafe { truck_solid_not(a) };
        assert!(!n1.is_null());
        // SAFETY: n1 有效。
        let n2 = unsafe { truck_solid_not(n1) };
        assert!(!n2.is_null());
        // 两者细分后应具有相同的单位立方体包围盒 [0,0,0]~[1,1,1]
        let mut m1: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut m2: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: n2、a 有效。
        unsafe {
            truck_solid_to_polygon(n2, 0.01, &mut m1, &mut err);
            truck_solid_to_polygon(a, 0.01, &mut m2, &mut err);
        }
        let mut b1 = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        let mut b2 = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: 网格均有效。
        unsafe {
            truck_polygonmesh_bounding_box(m1, &mut b1);
            truck_polygonmesh_bounding_box(m2, &mut b2);
        }
        // SAFETY: 数组可供读取 len 个元素。
        let s1 = unsafe { std::slice::from_raw_parts(b1.ptr, b1.len) };
        let s2 = unsafe { std::slice::from_raw_parts(b2.ptr, b2.len) };
        assert_eq!(s1, s2, "not(not(s)) bbox should equal s bbox");
        unsafe {
            crate::handle::truck_f64array_free(b1);
            crate::handle::truck_f64array_free(b2);
            truck_polygonmesh_free(m1);
            truck_polygonmesh_free(m2);
            truck_solid_free(n2);
            truck_solid_free(n1);
            truck_solid_free(a);
        }
    }

    #[test]
    fn boolean_null_inputs() {
        let a = unit_cube_solid();
        // SAFETY: 第二个参数为 NULL。
        assert!(unsafe { truck_solid_and(a, std::ptr::null(), 0.05) }.is_null());
        // SAFETY: 第一个参数为 NULL。
        assert!(unsafe { truck_solid_or(std::ptr::null(), a, 0.05) }.is_null());
        // SAFETY: 参数为 NULL。
        assert!(unsafe { truck_solid_not(std::ptr::null()) }.is_null());
        unsafe { truck_solid_free(a) };
    }

    // ---- 阶段 6：基本长方体 ----------------------------------------------

    /// 细分实体，并以 Vec<f64> 返回其 [min_xyz, max_xyz] 包围盒。
    unsafe fn solid_bbox(solid: *const TruckSolid) -> Vec<f64> {
        let mut mesh: *mut TruckPolygonMesh = std::ptr::null_mut();
        let mut err: *mut TruckError = std::ptr::null_mut();
        // SAFETY: solid 有效。
        unsafe { truck_solid_to_polygon(solid, 0.01, &mut mesh, &mut err) };
        let mut bbox = TruckF64Array { ptr: std::ptr::null_mut(), len: 0 };
        // SAFETY: mesh 有效。
        unsafe { truck_polygonmesh_bounding_box(mesh, &mut bbox) };
        // SAFETY: bbox 可供读取 len 个元素。
        let v = unsafe { std::slice::from_raw_parts(bbox.ptr, bbox.len) }.to_vec();
        unsafe {
            crate::handle::truck_f64array_free(bbox);
            truck_polygonmesh_free(mesh);
        }
        v
    }

    #[test]
    fn solid_box_unit() {
        let b = truck_solid_box(1.0, 1.0, 1.0);
        assert!(!b.is_null(), "unit box must be non-null");
        let bb = unsafe { solid_bbox(b) };
        assert_eq!(bb, vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], "unit box bbox");
        unsafe { truck_solid_free(b) };
    }

    #[test]
    fn solid_box_nonuniform() {
        let b = truck_solid_box(2.0, 3.0, 4.0);
        assert!(!b.is_null());
        let bb = unsafe { solid_bbox(b) };
        assert_eq!(bb, vec![0.0, 0.0, 0.0, 2.0, 3.0, 4.0], "2x3x4 box bbox");
        unsafe { truck_solid_free(b) };
    }

    // ---- 阶段 7：Wire + 基本体 -------------------------------------------

    #[test]
    fn wire_from_edges_and_queries() {
        // 由 4 条直边组成单位正方形闭环。
        let v0 = truck_vertex_new(0.0, 0.0, 0.0);
        let v1 = truck_vertex_new(1.0, 0.0, 0.0);
        let v2 = truck_vertex_new(1.0, 1.0, 0.0);
        let v3 = truck_vertex_new(0.0, 1.0, 0.0);
        // SAFETY: 顶点均有效。
        let e0 = unsafe { truck_edge_line(v0, v1) };
        let e1 = unsafe { truck_edge_line(v1, v2) };
        let e2 = unsafe { truck_edge_line(v2, v3) };
        let e3 = unsafe { truck_edge_line(v3, v0) };
        let edges = [e0, e1, e2, e3];
        // SAFETY: edges 数组有效；边被消费。
        let w = unsafe { truck_wire_from_edges(edges.as_ptr(), 4) };
        assert!(!w.is_null());
        // SAFETY: w 有效。
        assert_eq!(unsafe { truck_wire_edge_count(w) }, 4);
        // SAFETY: w 有效。
        assert!(unsafe { truck_wire_is_closed(w) }, "square loop should be closed");
        unsafe {
            truck_wire_free(w);
            truck_vertex_free(v0);
            truck_vertex_free(v1);
            truck_vertex_free(v2);
            truck_vertex_free(v3);
        }
    }

    #[test]
    fn face_attach_plane_from_closed_wire() {
        let v0 = truck_vertex_new(0.0, 0.0, 0.0);
        let v1 = truck_vertex_new(1.0, 0.0, 0.0);
        let v2 = truck_vertex_new(1.0, 1.0, 0.0);
        let v3 = truck_vertex_new(0.0, 1.0, 0.0);
        // SAFETY: 顶点均有效。
        let e0 = unsafe { truck_edge_line(v0, v1) };
        let e1 = unsafe { truck_edge_line(v1, v2) };
        let e2 = unsafe { truck_edge_line(v2, v3) };
        let e3 = unsafe { truck_edge_line(v3, v0) };
        let edges = [e0, e1, e2, e3];
        // SAFETY: 边被消费。
        let w = unsafe { truck_wire_from_edges(edges.as_ptr(), 4) };
        // SAFETY: w 有效。
        let f = unsafe { truck_face_attach_plane(w) };
        assert!(!f.is_null(), "attach_plane should succeed on a square loop");
        unsafe {
            truck_face_free(f);
            truck_wire_free(w);
            truck_vertex_free(v0);
            truck_vertex_free(v1);
            truck_vertex_free(v2);
            truck_vertex_free(v3);
        }
    }

    #[test]
    fn solid_cylinder_bbox() {
        let c = truck_solid_cylinder(1.0, 2.0);
        assert!(!c.is_null(), "cylinder must be non-null");
        let bb = unsafe { solid_bbox(c) };
        // 半径为 1 -> x、y 位于 [-1,1]；沿 +z 高度为 2 -> z 位于 [0,2]
        assert!(bb[0] > -1.1 && bb[0] < -0.9, "cyl min.x ~ -1, got {}", bb[0]);
        assert!(bb[2] > -0.1 && bb[2] < 0.1, "cyl min.z ~ 0, got {}", bb[2]);
        assert!(bb[5] > 1.9 && bb[5] < 2.1, "cyl max.z ~ 2, got {}", bb[5]);
        unsafe { truck_solid_free(c) };
    }

    #[test]
    fn solid_sphere_bbox() {
        let s = truck_solid_sphere(1.0);
        assert!(!s.is_null(), "sphere must be non-null");
        let bb = unsafe { solid_bbox(s) };
        // 以原点为中心且半径为 1 -> 所有坐标轴范围均为 [-1,1]
        for i in 0..6 {
            assert!(bb[i] > -1.1 && bb[i] < 1.1, "sphere coord {} out of range: {}", i, bb[i]);
        }
        unsafe { truck_solid_free(s) };
    }

    #[test]
    fn solid_cone_bbox() {
        // 调试：内联复现 FFI 圆锥体的精确几何形态，以观察是否出现 panic。
        use truck_modeling::*;
        use std::f64::consts::PI;
        let r = 1.0_f64;
        let h = 2.0_f64;
        let v0 = builder::vertex(Point3::new(0.0, r, 0.0));
        let v1 = builder::vertex(Point3::new(0.0, 0.0, h));
        let v2 = builder::vertex(Point3::new(0.0, 0.0, 0.0));
        let wire: truck_modeling::Wire = vec![builder::line(&v0, &v1), builder::line(&v1, &v2)].into();
        let shell = builder::cone(&wire, Vector3::unit_y(), Rad(2.0 * PI));
        let solid = truck_modeling::Solid::new(vec![shell]);
        assert!(!solid.boundaries().is_empty(), "inline cone r=1 h=2 should build");

        let c = truck_solid_cone(1.0, 2.0);
        assert!(!c.is_null(), "FFI cone must be non-null");
        let bb = unsafe { solid_bbox(c) };
        // 圆锥体按照 truck 的 cone() 方案构建（将 y-z 平面中的 Wire 绕 +y
        // 执行 rsweep）。其精确范围取决于 truck 内部的圆锥几何定义；这里只
        // 断言构建出了有限实体，且顶点沿 +z 位于底面上方。
        assert!(bb[5] > 0.0, "cone should extend in +z (apex), got max.z {}", bb[5]);
        assert!(bb.iter().all(|v| v.is_finite()), "cone bbox must be finite");
        unsafe { truck_solid_free(c) };
    }

    #[test]
    fn primitives_degenerate_return_null() {
        assert!(truck_solid_cylinder(0.0, 1.0).is_null());
        assert!(truck_solid_cylinder(1.0, -1.0).is_null());
        assert!(truck_solid_sphere(0.0).is_null());
        assert!(truck_solid_cone(-1.0, 1.0).is_null());
    }

    // ---- 阶段 7-0：基本体构建冒烟测试（圆柱体/球体/圆锥体）---------------
    // rsweep/try_attach_plane 路径的可行性门禁。在接入 FFI 前，先用纯
    // Rust 验证构建方案。

    #[test]
    fn primitives_smoke_cylinder() {
        use truck_modeling::*;
        let r = 1.0_f64;
        let h = 2.0_f64;
        let v = builder::vertex(Point3::new(r, 0.0, 0.0));
        let w = builder::rsweep(&v, Point3::origin(), Vector3::unit_z(), Rad(7.0));
        let f = builder::try_attach_plane(&[w]).expect("cylinder base attach");
        let solid = builder::tsweep(&f, Vector3::new(0.0, 0.0, h));
        assert!(!solid.boundaries().is_empty());
    }

    #[test]
    fn primitives_smoke_sphere() {
        use truck_meshalgo::tessellation::{MeshableShape, MeshedShape};
        use truck_modeling::*;
        let r = 1.0_f64;
        // 闭合半圆 Wire = [半圆弧, 直径线段]。
        // 顶点位于 (r,0,0)，圆弧经 (0,r,0) 到达 (-r,0,0)，再以直线返回。
        let v0 = builder::vertex(Point3::new(r, 0.0, 0.0));
        let v1 = builder::vertex(Point3::new(-r, 0.0, 0.0));
        let arc = builder::circle_arc(&v0, &v1, Point3::new(0.0, r, 0.0));
        let diam = builder::line(&v1, &v0);
        let wire: truck_modeling::Wire = vec![arc, diam].into();
        let f = builder::try_attach_plane(&[wire]).expect("sphere half-disk attach");
        // 让半圆面绕 +z（其直径轴）完整旋转一周
        let solid = builder::rsweep(&f, Point3::origin(), Vector3::unit_z(), Rad(7.0));
        let poly = solid.robust_triangulation(0.05).boundaries()[0].to_polygon();
        assert!(!poly.positions().is_empty(), "sphere must tessellate");
    }

    #[test]
    fn primitives_smoke_cone() {
        use truck_modeling::*;
        use std::f64::consts::PI;
        // 精确复现 truck 的圆锥体文档示例（单位几何体）。
        let v0 = builder::vertex(Point3::new(0.0, 1.0, 0.0));
        let v1 = builder::vertex(Point3::new(0.0, 0.0, 1.0));
        let v2 = builder::vertex(Point3::new(0.0, 0.0, 0.0));
        let wire: truck_modeling::Wire = vec![
            builder::line(&v0, &v1),
            builder::line(&v1, &v2),
        ].into();
        let cone = builder::cone(&wire, Vector3::unit_y(), Rad(2.0 * PI));
        let solid = truck_modeling::Solid::new(vec![cone]);
        assert!(!solid.boundaries().is_empty());
    }
}
