# Scene - componentized scene instance model skeleton
add_library(Scene STATIC
    scene.cpp
)

file(GLOB_RECURSE SCENE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${SCENE_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/scene/${rel}" COPYONLY)
endforeach()

target_include_directories(Scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(Scene PUBLIC Asset Engine)

set_target_properties(Scene PROPERTIES OUTPUT_NAME mulan_scene)

target_compile_definitions(Scene PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

