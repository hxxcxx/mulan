# Scene - componentized scene instance model skeleton
add_library(scene STATIC
    scene.cpp
)

mulan_copy_public_headers(scene scene)

target_link_libraries(scene PUBLIC asset math)

target_precompile_headers(scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

set_target_properties(scene PROPERTIES OUTPUT_NAME mulan_scene)

target_compile_definitions(scene PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
