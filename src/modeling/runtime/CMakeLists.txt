# runtime - 通用组装层：加载后端插件，不依赖任何具体后端
add_library(runtime STATIC
    runtime.cpp
)

mulan_copy_public_headers(runtime modeling/runtime)

target_link_libraries(runtime
    PRIVATE
        modeling_core
)

set_target_properties(runtime PROPERTIES OUTPUT_NAME mulan_runtime)

target_compile_definitions(runtime PRIVATE
    MULAN_DEFAULT_SHAPE_OPS_BACKEND="${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}"
    MULAN_ENABLE_OCCT_BACKEND=$<BOOL:${MULAN_ENABLE_OCCT_BACKEND}>
    MULAN_ENABLE_TRUCK_BACKEND=$<BOOL:${MULAN_ENABLE_TRUCK_BACKEND}>
    $<$<CONFIG:Debug>:_DEBUG>
)

if(WIN32)
    target_link_libraries(runtime PRIVATE wintrust)
endif()
