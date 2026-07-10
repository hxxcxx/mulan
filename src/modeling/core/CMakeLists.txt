# modeling_core - 中立 B-Rep 建模门面（零内核依赖）
# SHARED：被 io/asset/editor/后端DLL 共享，注册表单例跨边界只此一份。
add_library(modeling_core SHARED
    shape.cpp
    shape_file_reader.cpp
)

mulan_copy_public_headers(modeling_core modeling/core)

target_link_libraries(modeling_core
    PUBLIC
        math
        graphics
        core
)

set_target_properties(modeling_core PROPERTIES OUTPUT_NAME mulan_modeling_core)

target_compile_definitions(modeling_core PRIVATE
    BUILDING_MODELING_CORE
    $<$<CONFIG:Debug>:_DEBUG>
)
