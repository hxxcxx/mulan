# Asset - reusable document data skeleton
add_library(asset STATIC
    asset.cpp
    asset_library.cpp
    curve_asset.cpp
    curve_mesh_builder.cpp
)

file(GLOB_RECURSE ASSET_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${ASSET_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/asset/${rel}" COPYONLY)
endforeach()

target_include_directories(asset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(asset PUBLIC graphics)

target_precompile_headers(asset PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(asset PROPERTIES OUTPUT_NAME mulan_asset)

target_compile_definitions(asset PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
