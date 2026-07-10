//! 构建脚本：每次构建时通过 cbindgen 根据 Rust 源码重新生成
//! `include/truck_bridge.h`。该头文件属于构建产物，但会提交到仓库，
//! 以便使用方无需运行 Cargo 即可使用。

use std::path::PathBuf;

fn main() {
    let crate_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("include");
    let out_file = out_dir.join("truck_bridge.h");

    std::fs::create_dir_all(&out_dir).expect("create include/ dir");

    // src/ 下任意文件发生变化时重新运行（cbindgen 会读取源码）。
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    println!("cargo:rerun-if-changed=Cargo.toml");

    let cfg = cbindgen::Config::from_file(crate_dir.join("cbindgen.toml"))
        .expect("read cbindgen.toml");

    cbindgen::generate_with_config(&crate_dir, cfg)
        .expect("cbindgen generate")
        .write_to_file(&out_file);
}
