//! C ABI 的错误模型。
//!
//! ## 基础 #1 — 如何跨边界传递失败信息。
//!
//! 可能失败的函数：
//!   - 返回 `bool`（`false` 表示失败）或可为 NULL 的句柄；
//!   - 对于需要详细信息的失败，将 [`TruckError`] 句柄写入
//!     `*mut *mut TruckError` 输出参数。
//!
//! 来自 truck 或本胶合层的 panic **绝不会**跨越 FFI 边界展开。每个
//! `extern "C"` 函数体都使用 [`catch`]（或 [`truck_guard`] 宏）包裹实际
//! 工作，将 panic 转换为 [`TruckError`]。
//!
//! 本模块中的类型和函数有意使用简单签名（不使用 `FnOnce(...) -> ...`
//! 约束，也不使用 `dyn A + B`），使 cbindgen 的解析器能够顺利扫描此文件。

use crate::handle::{self, TruckStr};

/// 不透明错误句柄。C 只能看到 `typedef struct TruckError TruckError;`。
///
/// 保存一条 UTF-8 消息字符串。由本库分配，并通过 [`truck_error_free`] 释放。
#[derive(Debug)]
pub struct TruckError {
    message: String,
}

impl TruckError {
    /// 根据任意消息创建新错误。
    pub(crate) fn new(message: impl Into<String>) -> Self {
        TruckError { message: message.into() }
    }

    /// 以 `&str` 形式返回错误消息。
    pub(crate) fn message(&self) -> &str {
        &self.message
    }
}

/// 受保护 FFI 函数体的结果：成功值，或拥有所有权的错误句柄。
/// 保留为具名类型，使契约在各模块中表述一致，尽管 `truck_guard!` 宏目前会
/// 将其内联。
#[allow(dead_code)]
pub(crate) type GuardResult<T> = Result<T, *mut TruckError>;

/// 包装 `extern "C"` 函数体，使 panic 转换为 [`TruckError`]，而不是跨越
/// FFI 边界展开。
///
/// 用法：
/// ```ignore
/// #[no_mangle]
/// pub unsafe extern "C" fn truck_foo(h: *mut Thing, err: *mut *mut TruckError) -> bool {
///     let res = truck_guard!(|| {
///         let t = handle::from_ref(h).ok_or_else(|| TruckError::new("null handle"))?;
///         do_work(t)
///     });
///     finish(res, err)
/// }
/// ```
///
/// 此功能以 `macro_rules!` 宏实现（cbindgen 会跳过该宏），而不是使用泛型
/// 函数，因此本文件中不会出现导致 cbindgen 解析器出错的
/// `FnOnce() -> ...` 约束。
macro_rules! truck_guard {
    ($body:expr) => {{
        match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe($body)) {
            ::std::result::Result::Ok(::std::result::Result::Ok(v)) =>
                ::std::result::Result::Ok(v),
            ::std::result::Result::Ok(::std::result::Result::Err(e)) =>
                ::std::result::Result::Err($crate::handle::into_raw(e)),
            ::std::result::Result::Err(payload) => {
                let msg = $crate::error::panic_to_string(&payload);
                ::std::result::Result::Err($crate::handle::into_raw(
                    $crate::error::TruckError::new(msg),
                ))
            }
        }
    }};
}
pub(crate) use truck_guard;

/// 按照 FFI `(bool, *err)` 契约处理 [`truck_guard!`] 的结果：
///   - `Ok(v)`  → 执行 `$ok(v)`（写入成功输出参数）。
///   - `Err(e)` → 若 `$err` 非 NULL，则将错误句柄写入 `*$err`；否则丢弃
///     错误句柄以避免泄漏。最后返回 `false`。
///
/// `$res` 是由 `truck_guard!` 生成的 `Result<T, *mut TruckError>`。
///
/// 这是错误处理流水线的后半部分（guard 生成，deliver 消费），由所有暴露
/// `err` 输出参数的模块共享。它在 crate 根目录导出，使所有模块都能调用
/// `crate::truck_deliver!(...)`；`#[doc(hidden)]` 使其不出现在公共 API 文档中。
#[macro_export]
#[doc(hidden)]
macro_rules! truck_deliver {
    ($res:expr, $err:expr, $ok:expr) => {
        match $res {
            ::std::result::Result::Ok(v) => {
                $ok(v);
                true
            }
            ::std::result::Result::Err(e) => {
                if !$err.is_null() {
                    // SAFETY: 调用方保证 err 指向可写存储空间。
                    unsafe { *$err = e };
                } else {
                    // 没有接收方，因此收回句柄以避免泄漏。
                    // SAFETY: e 是刚分配且拥有所有权的句柄。
                    unsafe { $crate::handle::take_raw::<$crate::error::TruckError>(e) };
                }
                false
            }
        }
    };
}

/// 尽可能将 panic 载荷（`Box<dyn Any + Send>`）转换为字符串。
///
/// 使用接收 trait object 的普通函数形式编写，使 cbindgen 能够解析。
pub(crate) fn panic_to_string(payload: &Box<dyn std::any::Any + Send>) -> String {
    use std::any::Any;
    // `payload` 是 `&Box<dyn Any + Send>`；访问内部的 `dyn Any`，使下面的
    // 向下转型匹配构造 panic 时使用的值，而不是 Box 包装器本身。
    let any: &dyn Any = &**payload;
    if let Some(s) = any.downcast_ref::<&'static str>() {
        format!("internal panic: {}", s)
    } else if let Some(s) = any.downcast_ref::<String>() {
        format!("internal panic: {}", s)
    } else {
        "internal panic: <non-string payload>".to_string()
    }
}

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

/// 将错误消息复制为 UTF-8 字节字符串。
///
/// 返回的 [`TruckStr`] 是由调用方拥有的新内存分配；使用 [`truck_str_free`]
/// 释放。传入 `NULL` 将得到空的 `TruckStr`。
///
/// # 安全性
/// `err` 必须为 NULL，或者是由生成错误句柄的 truck-bridge 函数返回的有效指针。
#[no_mangle]
pub unsafe extern "C" fn truck_error_message(err: *const TruckError) -> TruckStr {
    // SAFETY: 调用方保证 err 为 NULL 或有效的 TruckError 句柄。
    match unsafe { handle::from_ref(err) } {
        Some(e) => TruckStr::from(e.message().as_bytes().to_vec()),
        None => TruckStr::empty(),
    }
}

/// 释放错误句柄。此操作具有幂等性：`truck_error_free(NULL)` 不执行任何操作。
///
/// # 安全性
/// `err` 必须为 NULL，或者是先前由 truck-bridge 错误输出参数返回且尚未释放的指针。
#[no_mangle]
pub unsafe extern "C" fn truck_error_free(err: *mut TruckError) {
    // SAFETY: 调用方保证 err 为 NULL 或有效且拥有所有权的句柄。
    match unsafe { handle::take_raw(err) } {
        Some(e) => drop(e),
        None => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run_guarded<F: FnOnce() -> Result<i32, TruckError>>(f: F) -> GuardResult<i32> {
        truck_guard!(f)
    }

    #[test]
    fn guard_catches_panic() {
        let res = run_guarded(|| -> Result<i32, TruckError> {
            panic!("boom");
        });
        let err_ptr = res.expect_err("panic must become Err");
        // SAFETY: err_ptr 来自 guard()，其所有权归当前代码所有。
        let msg = unsafe { &*err_ptr }.message();
        assert!(msg.contains("internal panic"), "got: {msg}");
        assert!(msg.contains("boom"), "got: {msg}");
        // SAFETY: 释放当前代码拥有的句柄。
        unsafe { truck_error_free(err_ptr) };
    }

    #[test]
    fn guard_propagates_logical_error() {
        let res = run_guarded(|| Err(TruckError::new("parse failed")));
        let err_ptr = res.expect_err("logical error must be Err");
        // SAFETY: err_ptr 的所有权归当前代码所有。
        assert_eq!(unsafe { &*err_ptr }.message(), "parse failed");
        unsafe { truck_error_free(err_ptr) };
    }

    #[test]
    fn guard_ok_on_success() {
        let res = run_guarded(|| Ok::<_, TruckError>(42));
        assert_eq!(res.unwrap(), 42);
    }

    #[test]
    fn error_message_null_yields_empty() {
        // SAFETY: 明确允许传入 NULL。
        let s = unsafe { truck_error_message(std::ptr::null()) };
        assert_eq!(s.len, 0);
        // SAFETY: s 是空的 TruckStr（指针为 NULL），释放函数允许该值。
        unsafe { crate::handle::truck_str_free(s) };
    }
}
