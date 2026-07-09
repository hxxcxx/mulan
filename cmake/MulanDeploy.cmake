function(mulan_collect_named_dlls out_var source_dir)
    set(options RECURSE)
    set(multi_value_args NAMES EXCLUDE_REGEX)
    cmake_parse_arguments(ARG "${options}" "" "${multi_value_args}" ${ARGN})

    if(NOT source_dir OR NOT EXISTS "${source_dir}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    if(ARG_RECURSE)
        file(GLOB_RECURSE candidates CONFIGURE_DEPENDS "${source_dir}/*.dll")
    else()
        file(GLOB candidates CONFIGURE_DEPENDS "${source_dir}/*.dll")
    endif()

    set(result)
    set(seen_names)
    foreach(dll IN LISTS candidates)
        get_filename_component(dll_name "${dll}" NAME)
        if(ARG_NAMES AND NOT dll_name IN_LIST ARG_NAMES)
            continue()
        endif()

        file(TO_CMAKE_PATH "${dll}" normalized_dll)
        set(skip FALSE)
        foreach(pattern IN LISTS ARG_EXCLUDE_REGEX)
            if(normalized_dll MATCHES "${pattern}")
                set(skip TRUE)
                break()
            endif()
        endforeach()
        if(skip)
            continue()
        endif()

        if(dll_name IN_LIST seen_names)
            continue()
        endif()

        list(APPEND seen_names "${dll_name}")
        list(APPEND result "${dll}")
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(mulan_copy_files_post_build target)
    set(multi_value_args FILES)
    cmake_parse_arguments(ARG "" "" "${multi_value_args}" ${ARGN})

    foreach(dll IN LISTS ARG_FILES)
        if(NOT EXISTS "${dll}")
            continue()
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${target}>"
        )
    endforeach()
endfunction()

function(mulan_copy_dlls_post_build target source_dir)
    set(options RECURSE)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    set(collect_args ${ARG_UNPARSED_ARGUMENTS})
    if(ARG_RECURSE)
        list(APPEND collect_args RECURSE)
    endif()
    mulan_collect_named_dlls(dlls "${source_dir}" ${collect_args})
    mulan_copy_files_post_build(${target} FILES ${dlls})
endfunction()
