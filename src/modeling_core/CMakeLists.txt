# modeling_core - 中立 B-Rep 建模门面（零内核依赖）
add_library(modeling_core STATIC
    shape.cpp
    shape_file_reader.cpp
)

mulan_copy_public_headers(modeling_core modeling_core)

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
