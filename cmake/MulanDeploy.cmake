function(mulan_copy_dlls_post_build target source_dir)
    set(options RECURSE)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    if(NOT source_dir OR NOT EXISTS "${source_dir}")
        return()
    endif()

    if(ARG_RECURSE)
        file(GLOB_RECURSE dlls CONFIGURE_DEPENDS "${source_dir}/*.dll")
    else()
        file(GLOB dlls CONFIGURE_DEPENDS "${source_dir}/*.dll")
    endif()

    foreach(dll IN LISTS dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${target}>"
        )
    endforeach()
endfunction()
