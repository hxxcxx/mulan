//! ABI 版本管理。
//!
//! ## 基础 #3 — 在加载时拒绝 ABI 不匹配的 `.dll`。
//!
//! 本库的版本是构建期常量。使用方应在启动时断言 `truck_abi_version()` 与
//! 编译时所用头文件中的 `TRUCK_BRIDGE_ABI_VERSION` 宏一致。这样，链接到
//! 旧版库（其不透明句柄布局可能不同）时会明确失败，而不是破坏内存。

use crate::handle::TruckStr;

/// 任何可能改变导出项的 C 可见布局或语义的变更都应递增此版本。该值必须与
/// cbindgen 写入头文件的 `TRUCK_BRIDGE_ABI_VERSION` 宏保持同步。
pub const TRUCK_BRIDGE_ABI_VERSION: u32 = 1;

/// 便于阅读的版本字符串，在编译时由 Cargo 环境变量组成。
const VERSION_STRING: &str = concat!(
    "truck-bridge ",
    env!("CARGO_PKG_VERSION"),
    " / truck-polymesh 0.6.0",
);

/// 返回 ABI 版本。使用方应将其与所用头文件中的
/// `TRUCK_BRIDGE_ABI_VERSION` 宏进行比较。
///
/// 返回的 `u32` 是普通值类型，不涉及所有权问题。
#[no_mangle]
pub extern "C" fn truck_abi_version() -> u32 {
    TRUCK_BRIDGE_ABI_VERSION
}

/// 返回构建/版本描述字符串（UTF-8 字节，不以 NUL 结尾）。
/// 请使用 [`crate::handle::truck_str_free`] 释放。
#[no_mangle]
pub extern "C" fn truck_version_string() -> TruckStr {
    TruckStr::from(VERSION_STRING.as_bytes().to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_version_is_one() {
        assert_eq!(truck_abi_version(), 1);
        assert_eq!(TRUCK_BRIDGE_ABI_VERSION, 1);
    }

    #[test]
    fn version_string_is_freed_safely() {
        let s = truck_version_string();
        assert!(s.len > 0);
        // SAFETY: s 由 truck_version_string 返回。
        unsafe { crate::handle::truck_str_free(s) };
    }
}
