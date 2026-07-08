# Asset - reusable document data skeleton
add_library(asset STATIC
    asset.cpp
    asset_library.cpp
    curve_asset.cpp
    curve_mesh_builder.cpp
    face_asset.cpp
)

mulan_copy_public_headers(asset asset)

target_link_libraries(asset PUBLIC graphics)

target_precompile_headers(asset PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(asset PROPERTIES OUTPUT_NAME mulan_asset)

target_compile_definitions(asset PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
