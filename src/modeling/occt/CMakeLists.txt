# modeling_occt - OCCT 后端（SHARED 插件，API 完全不暴露）
#
# 编成 DLL，只导出一个 C 符号 mulan_load_backend（见 backend_entry.cpp）。
# 所有 C++ 类不导出；头不发布到公共 include 目录。
# 由 runtime 经 LoadLibrary 加载，任何人不在链接期/源码期依赖本模块。
add_library(modeling_occt SHARED
    occt_shape_storage.cpp
    occt_tessellator_internal.cpp
    shape_factory.cpp
    step_reader.cpp
    backend_entry.cpp
)

target_link_libraries(modeling_occt
    PRIVATE
        modeling_core
        OCCT::OCCT
)

set_target_properties(modeling_occt PROPERTIES OUTPUT_NAME mulan_modeling_occt)

target_compile_definitions(modeling_occt PRIVATE
    BUILDING_MODELING_OCCT
    $<$<CONFIG:Debug>:_DEBUG>
)

# 部署后端 DLL 到可执行文件同目录的 backends/，供 runtime 扫描加载。
set(OCCT_BACKEND_DEST "$<TARGET_FILE_DIR:mulan>/backends")
add_custom_command(TARGET modeling_occt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OCCT_BACKEND_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:modeling_occt>" "${OCCT_BACKEND_DEST}/"
    COMMENT "Deploying OCCT backend DLL to backends/..."
)
