function(mulan_copy_public_headers target module)
    set(generated_include_dir "${CMAKE_BINARY_DIR}/include/mulan/${module}")
    file(REMOVE_RECURSE "${generated_include_dir}")

    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    )

    foreach(header IN LISTS public_headers)
        file(RELATIVE_PATH relative_header "${CMAKE_CURRENT_SOURCE_DIR}" "${header}")
        configure_file("${header}" "${generated_include_dir}/${relative_header}" COPYONLY)
    endforeach()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${target} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        )
    else()
        target_include_directories(${target} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        )
    endif()
endfunction()
