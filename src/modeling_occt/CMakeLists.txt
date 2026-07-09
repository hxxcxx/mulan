# modeling_occt - OCCT 后端实现（OCCT 唯一合法驻地）
#
# OCCT 在此 PRIVATE 链接，不向依赖本模块的 io/asset 传播。
# 本模块对外公共头（step_reader.h）零 OCCT 类型。
add_library(modeling_occt STATIC
    occt_shape_storage.cpp
    occt_tessellator_internal.cpp
    shape_factory.cpp
    step_reader.cpp
)

mulan_copy_public_headers(modeling_occt modeling_occt)

target_link_libraries(modeling_occt
    PUBLIC
        modeling_core
    PRIVATE
        OCCT::OCCT
)

set_target_properties(modeling_occt PROPERTIES OUTPUT_NAME mulan_modeling_occt)

target_compile_definitions(modeling_occt PRIVATE
    BUILDING_MODELING_OCCT
    $<$<CONFIG:Debug>:_DEBUG>
)
