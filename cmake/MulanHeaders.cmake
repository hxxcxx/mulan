function(mulan_copy_public_headers target module)
    set(generated_include_dir "${CMAKE_BINARY_DIR}/include/mulan/${module}")
    file(MAKE_DIRECTORY "${generated_include_dir}")

    if(ARGN)
        set(public_headers)
        foreach(relative_header IN LISTS ARGN)
            list(APPEND public_headers "${CMAKE_CURRENT_SOURCE_DIR}/${relative_header}")
        endforeach()
    else()
        file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
            "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
        )

        # 排除 detail/ 子目录和预编译头：内部实现不对外暴露。
        list(FILTER public_headers EXCLUDE REGEX "/detail/")
        list(FILTER public_headers EXCLUDE REGEX "(^|/)pch\\.(h|hh|hpp|hxx)$")
    endif()

    set(generated_headers)
    foreach(header IN LISTS public_headers)
        file(RELATIVE_PATH relative_header "${CMAKE_CURRENT_SOURCE_DIR}" "${header}")
        set(generated_header "${generated_include_dir}/${relative_header}")
        list(APPEND generated_headers "${generated_header}")
        configure_file("${header}" "${generated_header}" COPYONLY)
    endforeach()

    file(GLOB_RECURSE existing_generated_headers
        "${generated_include_dir}/*.h"
        "${generated_include_dir}/*.hh"
        "${generated_include_dir}/*.hpp"
        "${generated_include_dir}/*.hxx"
    )
    foreach(existing_header IN LISTS existing_generated_headers)
        if(NOT existing_header IN_LIST generated_headers)
            file(REMOVE "${existing_header}")
        endif()
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
