vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wolfpld/tracy
    REF "v${VERSION}"
    SHA512 18c0c589a1d97d0760958c8ab00ba2135bc602fd359d48445b5d8ed76e5b08742d818bb8f835b599149030f455e553a92db86fb7bae049b47820e4401cf9f935
    HEAD_REF master
)

# 本项目只允许同机 Tracy Viewer 连接：
# - TCP 仅绑定回环地址，不向局域网暴露监听端口；
# - 编译移除 UDP 客户端发现广播；
# - 关闭系统级自动追踪，只保留项目显式插入的性能区段；
# - 禁用第三方崩溃处理器，避免改变应用既有错误处理语义。
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DTRACY_ONLY_LOCALHOST=ON
        -DTRACY_NO_BROADCAST=ON
        -DTRACY_NO_SYSTEM_TRACING=ON
        -DTRACY_NO_CRASH_HANDLER=ON
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME Tracy CONFIG_PATH "lib/cmake/Tracy")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
