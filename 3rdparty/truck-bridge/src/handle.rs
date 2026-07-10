//! C ABI 的所有权基础设施。
//!
//! ## 基础 #2 — 跨边界对象的所有权归属。
//!
//! - truck 对象使用**不透明句柄**：C 获得 `typedef struct X X;`，并且只会
//!   接触 `X*`。真实类型位于 Rust 分配器管理的内存中，通过 [`into_raw`] /
//!   [`take_raw`] / [`from_ref`] / [`from_mut`] 访问。
//! - **自有字节/数值数组**（`TruckF64Array`、`TruckF32Array`、
//!   `TruckU8Array`、`TruckU32Array`、`TruckStr`）：交给 C 的 `{ptr, len}`
//!   视图，指向由 Rust 分配的内存。调用方必须使用匹配的 `*_free` 释放。
//!   切勿对其调用 `free()`/`delete`，因为它们属于 Rust 分配器。

use std::mem::ManuallyDrop;

// ---------------------------------------------------------------------------
// 不透明句柄辅助函数
// ---------------------------------------------------------------------------

/// 将值装入 Box 并把拥有所有权的裸指针交给 C。之后必须通过 [`take_raw`]
///（或匹配的 `*_free`）传回该指针。
pub(crate) fn into_raw<T>(t: T) -> *mut T {
    Box::into_raw(Box::new(t))
}

/// 收回由 [`into_raw`] 生成的指针的所有权。传入 NULL 时返回 `None`。
///
/// # 安全性
/// `p` 必须为 NULL，或者是先前由 [`into_raw`] 生成且尚未被收回的指针。
pub(crate) unsafe fn take_raw<T>(p: *mut T) -> Option<T> {
    if p.is_null() {
        return None;
    }
    // SAFETY: 调用方保证 p 非空、有效、拥有所有权，且源自 Box。
    Some(*unsafe { Box::from_raw(p) })
}

/// 借用 const 指针。传入 NULL 时返回 `None`。
///
/// # 安全性
/// `p` 必须为 NULL，或者是指向有效 `T` 的有效指针，且该 `T` 的生命周期
/// 长于返回的引用。
pub(crate) unsafe fn from_ref<'a, T>(p: *const T) -> Option<&'a T> {
    if p.is_null() {
        return None;
    }
    // SAFETY: 调用方保证指针有效且生命周期满足要求。
    Some(unsafe { &*p })
}

/// 借用 mut 指针。传入 NULL 时返回 `None`。
///
/// # 安全性
/// `p` 必须为 NULL，或者是唯一可达且指向有效 `T` 的有效指针，且该 `T`
/// 的生命周期长于返回的引用。
pub(crate) unsafe fn from_mut<'a, T>(p: *mut T) -> Option<&'a mut T> {
    if p.is_null() {
        return None;
    }
    // SAFETY: 调用方保证指针有效、访问唯一且生命周期满足要求。
    Some(unsafe { &mut *p })
}

// ---------------------------------------------------------------------------
// 交给 C 的自有数组/字符串视图
// ---------------------------------------------------------------------------

/// 交给 C 的自有 `f64` 数组。使用 [`truck_f64array_free`] 释放。
#[repr(C)]
#[derive(Debug)]
pub struct TruckF64Array {
    /// 指向首个元素的指针（当且仅当 `len == 0` 时为 NULL）。
    pub ptr: *mut f64,
    /// 元素数量。
    pub len: usize,
}

/// 交给 C 的自有 `f32` 数组。使用 [`truck_f32array_free`] 释放。
#[repr(C)]
#[derive(Debug)]
pub struct TruckF32Array {
    pub ptr: *mut f32,
    pub len: usize,
}

/// 交给 C 的自有 `u8` 字节数组。使用 [`truck_u8array_free`] 释放。
#[repr(C)]
#[derive(Debug)]
pub struct TruckU8Array {
    pub ptr: *mut u8,
    pub len: usize,
}

/// 交给 C 的自有 `u32` 数组。使用 [`truck_u32array_free`] 释放。
#[repr(C)]
#[derive(Debug)]
pub struct TruckU32Array {
    pub ptr: *mut u32,
    pub len: usize,
}

/// 交给 C 的自有 UTF-8 字节字符串。**不以 NUL 结尾。**
/// 使用 [`truck_str_free`] 释放。
#[repr(C)]
#[derive(Debug)]
pub struct TruckStr {
    pub ptr: *mut u8,
    pub len: usize,
}

/// 在不运行析构函数的情况下，将 `Vec<T>` 拆分为 `(ptr, len, capacity)`，
/// 并将所有权转移给调用方。
pub(crate) fn vec_into_raw_parts<T>(mut v: Vec<T>) -> (*mut T, usize, usize) {
    // 同时保留分配指针与原始容量，使匹配的 `*_free` 能准确重建并丢弃 Vec。
    let ptr = v.as_mut_ptr();
    let len = v.len();
    let cap = v.capacity();
    let _ = ManuallyDrop::new(v);
    (ptr, len, cap)
}

/// 将 `{ptr,len}` 视图收回并重建为 `Vec<T>`，以便将其丢弃。
///
/// # 安全性
/// `ptr/len/cap` 必须描述先前由 [`vec_into_raw_parts`] 生成的有效内存分配；
/// 或者 `ptr` 可为 NULL，但此时必须满足 `len == cap == 0`。
pub(crate) unsafe fn vec_from_raw_parts<T>(ptr: *mut T, len: usize, cap: usize) -> Vec<T> {
    if ptr.is_null() {
        return Vec::new();
    }
    // SAFETY: 调用方保证这些部分来自匹配的 Vec。
    unsafe { Vec::from_raw_parts(ptr, len, cap) }
}

/// 辅助函数：先收缩 Vec 使 capacity == len，再生成 `(ptr, len)` 视图
///（释放路径始终按 cap == len 恢复）。
fn shrinked_view<T>(mut v: Vec<T>) -> (*mut T, usize) {
    v.shrink_to_fit();
    debug_assert_eq!(v.len(), v.capacity());
    let (ptr, len, _cap) = vec_into_raw_parts(v);
    (ptr, len)
}

impl TruckF64Array {
    /// 包装 `Vec<f64>`，并将其内存分配的所有权转移给调用方（C）。
    pub(crate) fn from(v: Vec<f64>) -> Self {
        let (ptr, len) = shrinked_view(v);
        Self { ptr, len }
    }
}

impl TruckF32Array {
    pub(crate) fn from(v: Vec<f32>) -> Self {
        let (ptr, len) = shrinked_view(v);
        Self { ptr, len }
    }
}

impl TruckU8Array {
    pub(crate) fn from(v: Vec<u8>) -> Self {
        let (ptr, len) = shrinked_view(v);
        Self { ptr, len }
    }
}

impl TruckU32Array {
    pub(crate) fn from(v: Vec<u32>) -> Self {
        let (ptr, len) = shrinked_view(v);
        Self { ptr, len }
    }
}

impl TruckStr {
    pub(crate) fn from(v: Vec<u8>) -> Self {
        let (ptr, len) = shrinked_view(v);
        Self { ptr, len }
    }

    /// 空视图（NULL 指针、长度为零），可安全释放。
    pub(crate) fn empty() -> Self {
        Self { ptr: std::ptr::null_mut(), len: 0 }
    }
}

// ---------------------------------------------------------------------------
// C ABI — 数组/字符串释放函数
// ---------------------------------------------------------------------------

/// 释放先前由 truck-bridge 返回的 `TruckF64Array`。对于空数组或零长度数组，
/// 此操作具有幂等性。
///
/// # 安全性
/// `arr` 必须描述先前由 truck-bridge 生成的内存分配（或为零初始化的空值），
/// 并且不得已经被释放。
#[no_mangle]
pub unsafe extern "C" fn truck_f64array_free(arr: TruckF64Array) {
    if arr.ptr.is_null() {
        return;
    }
    // SAFETY: 由 TruckF64Array::from 生成，且 cap == len。
    drop(unsafe { vec_from_raw_parts::<f64>(arr.ptr, arr.len, arr.len) });
}

/// 释放 `TruckF32Array`。对于空数组，此操作具有幂等性。安全契约参见
/// [`truck_f64array_free`]。
#[no_mangle]
pub unsafe extern "C" fn truck_f32array_free(arr: TruckF32Array) {
    if arr.ptr.is_null() {
        return;
    }
    drop(unsafe { vec_from_raw_parts::<f32>(arr.ptr, arr.len, arr.len) });
}

/// 释放 `TruckU8Array`。对于空数组，此操作具有幂等性。安全契约参见
/// [`truck_f64array_free`]。
#[no_mangle]
pub unsafe extern "C" fn truck_u8array_free(arr: TruckU8Array) {
    if arr.ptr.is_null() {
        return;
    }
    drop(unsafe { vec_from_raw_parts::<u8>(arr.ptr, arr.len, arr.len) });
}

/// 释放 `TruckU32Array`。对于空数组，此操作具有幂等性。安全契约参见
/// [`truck_f64array_free`]。
#[no_mangle]
pub unsafe extern "C" fn truck_u32array_free(arr: TruckU32Array) {
    if arr.ptr.is_null() {
        return;
    }
    drop(unsafe { vec_from_raw_parts::<u32>(arr.ptr, arr.len, arr.len) });
}

/// 释放 `TruckStr`。对于空字符串，此操作具有幂等性。安全契约参见
/// [`truck_f64array_free`]。
#[no_mangle]
pub unsafe extern "C" fn truck_str_free(s: TruckStr) {
    if s.ptr.is_null() {
        return;
    }
    drop(unsafe { vec_from_raw_parts::<u8>(s.ptr, s.len, s.len) });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_ref_null_is_none() {
        // SAFETY: 明确允许传入 NULL。
        assert!(unsafe { from_ref::<i32>(std::ptr::null()) }.is_none());
    }

    #[test]
    fn into_and_take_roundtrip() {
        let p = into_raw(123i32);
        // SAFETY: p 来自 into_raw，尚未被收回。
        assert_eq!(unsafe { take_raw(p) }, Some(123));
        // 再次收回同一地址会触发 UB；此处只检查 NULL 情况：
        assert!(unsafe { take_raw::<i32>(std::ptr::null_mut()) }.is_none());
    }

    #[test]
    fn f64array_from_and_free_preserves_data() {
        let arr = TruckF64Array::from(vec![1.5, -2.0, 3.25]);
        assert_eq!(arr.len, 3);
        // SAFETY: arr 刚刚生成，arr.ptr 可供读取 arr.len 个元素。
        let slice = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert_eq!(slice, &[1.5, -2.0, 3.25]);
        // SAFETY: arr 来自 TruckF64Array::from。
        unsafe { truck_f64array_free(arr) };
    }

    #[test]
    fn f32array_from_and_free_preserves_data() {
        let arr = TruckF32Array::from(vec![1.5_f32, -2.0, 3.25]);
        assert_eq!(arr.len, 3);
        // SAFETY: arr 刚刚生成，arr.ptr 可供读取 arr.len 个元素。
        let slice = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert_eq!(slice, &[1.5_f32, -2.0, 3.25]);
        // SAFETY: arr 来自 TruckF32Array::from。
        unsafe { truck_f32array_free(arr) };
    }

    #[test]
    fn u32array_from_and_free_preserves_data() {
        let arr = TruckU32Array::from(vec![1u32, 2, 3]);
        assert_eq!(arr.len, 3);
        // SAFETY: arr 刚刚生成，arr.ptr 可供读取 arr.len 个元素。
        let slice = unsafe { std::slice::from_raw_parts(arr.ptr, arr.len) };
        assert_eq!(slice, &[1u32, 2, 3]);
        // SAFETY: arr 来自 TruckU32Array::from。
        unsafe { truck_u32array_free(arr) };
    }

    #[test]
    fn empty_str_free_is_noop() {
        // SAFETY: 明确允许传入空值。
        unsafe { truck_str_free(TruckStr::empty()) };
    }

    #[test]
    fn str_from_bytes_roundtrip() {
        let s = TruckStr::from(b"hello".to_vec());
        assert_eq!(s.len, 5);
        // SAFETY: s 刚刚生成，指针可供读取 s.len 个字节。
        let bytes = unsafe { std::slice::from_raw_parts(s.ptr, s.len) };
        assert_eq!(bytes, b"hello");
        // SAFETY: s 来自 TruckStr::from。
        unsafe { truck_str_free(s) };
    }
}
