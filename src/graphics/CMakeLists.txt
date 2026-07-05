add_library(graphics INTERFACE)

file(GLOB_RECURSE GRAPHICS_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${GRAPHICS_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/graphics/${rel}" COPYONLY)
endforeach()

target_include_directories(graphics INTERFACE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(graphics INTERFACE math)
