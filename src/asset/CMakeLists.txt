# Asset - reusable document data skeleton
add_library(Asset STATIC
    asset.cpp
    asset_library.cpp
)

file(GLOB_RECURSE ASSET_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${ASSET_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/asset/${rel}" COPYONLY)
endforeach()

target_include_directories(Asset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(Asset PUBLIC Engine)

set_target_properties(Asset PROPERTIES OUTPUT_NAME mulan_asset)

target_compile_definitions(Asset PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

