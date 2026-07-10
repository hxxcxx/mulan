# runtime - 通用组装层：加载后端插件，不依赖任何具体后端
add_library(runtime STATIC
    runtime.cpp
)

mulan_copy_public_headers(runtime modeling/runtime)

target_link_libraries(runtime
    PUBLIC
        modeling_core
)

set_target_properties(runtime PROPERTIES OUTPUT_NAME mulan_runtime)

target_compile_definitions(runtime PRIVATE
    BUILDING_RUNTIME
    MULAN_DEFAULT_SHAPE_OPS_BACKEND="${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}"
    $<$<CONFIG:Debug>:_DEBUG>
)
