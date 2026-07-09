function(mulan_collect_runtime_bin_dirs out_var root_dir)
    set(result)
    if(NOT root_dir OR NOT EXISTS "${root_dir}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    set(candidate_dirs
        "${root_dir}"
        "${root_dir}/bin"
    )
    file(GLOB package_bin_dirs LIST_DIRECTORIES true
        "${root_dir}/*/bin"
        "${root_dir}/*/bin/win64"
        "${root_dir}/*/bin/x64"
        "${root_dir}/*/redist"
        "${root_dir}/*/redist/win64"
        "${root_dir}/*/redist/x64"
    )
    list(APPEND candidate_dirs ${package_bin_dirs})

    foreach(candidate_dir IN LISTS candidate_dirs)
        if(IS_DIRECTORY "${candidate_dir}")
            file(TO_CMAKE_PATH "${candidate_dir}" normalized_dir)
            if(NOT normalized_dir IN_LIST result)
                list(APPEND result "${normalized_dir}")
            endif()
        endif()
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(mulan_find_named_dlls out_var)
    set(options REQUIRED)
    set(multi_value_args NAMES SEARCH_DIRS)
    cmake_parse_arguments(ARG "${options}" "" "${multi_value_args}" ${ARGN})

    set(result)
    set(missing)
    foreach(dll_name IN LISTS ARG_NAMES)
        string(MAKE_C_IDENTIFIER "${dll_name}" dll_id)
        find_file(MULAN_FOUND_DLL_${dll_id}
            NAMES "${dll_name}"
            PATHS ${ARG_SEARCH_DIRS}
            NO_DEFAULT_PATH
            NO_CACHE
        )

        if(MULAN_FOUND_DLL_${dll_id})
            list(APPEND result "${MULAN_FOUND_DLL_${dll_id}}")
        else()
            list(APPEND missing "${dll_name}")
        endif()
    endforeach()

    if(ARG_REQUIRED AND missing)
        list(JOIN missing ", " missing_text)
        message(FATAL_ERROR "Runtime DLLs not found: ${missing_text}")
    elseif(missing)
        list(JOIN missing ", " missing_text)
        message(WARNING "Runtime DLLs not found: ${missing_text}")
    endif()

    list(REMOVE_DUPLICATES result)
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(mulan_configure_windows_debug_runtime target)
    set(one_value_args QT_PLUGIN_DIR QT_PLATFORM_PLUGIN_DIR WORKING_DIRECTORY)
    set(multi_value_args PATHS ENVIRONMENT)
    cmake_parse_arguments(ARG "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT WIN32)
        return()
    endif()

    set(runtime_paths)
    foreach(path IN LISTS ARG_PATHS)
        if(NOT path)
            continue()
        endif()

        if("${path}" MATCHES "^\\$<")
            list(APPEND runtime_paths "${path}")
        elseif(IS_DIRECTORY "${path}")
            file(TO_CMAKE_PATH "${path}" normalized_path)
            list(APPEND runtime_paths "${normalized_path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES runtime_paths)

    set(debug_environment ${ARG_ENVIRONMENT})
    if(runtime_paths)
        list(JOIN runtime_paths ";" runtime_path)
        string(REPLACE ";" "\\;" runtime_path "${runtime_path}")
        list(PREPEND debug_environment "PATH=${runtime_path}\\;%PATH%")
    endif()

    if(ARG_QT_PLUGIN_DIR AND IS_DIRECTORY "${ARG_QT_PLUGIN_DIR}")
        file(TO_CMAKE_PATH "${ARG_QT_PLUGIN_DIR}" qt_plugin_dir)
        list(APPEND debug_environment "QT_PLUGIN_PATH=${qt_plugin_dir}")
    endif()

    if(ARG_QT_PLATFORM_PLUGIN_DIR AND IS_DIRECTORY "${ARG_QT_PLATFORM_PLUGIN_DIR}")
        file(TO_CMAKE_PATH "${ARG_QT_PLATFORM_PLUGIN_DIR}" qt_platform_plugin_dir)
        list(APPEND debug_environment "QT_QPA_PLATFORM_PLUGIN_PATH=${qt_platform_plugin_dir}")
    endif()

    if(debug_environment)
        set_property(TARGET ${target} PROPERTY VS_DEBUGGER_ENVIRONMENT "${debug_environment}")
    endif()

    if(ARG_WORKING_DIRECTORY)
        set_property(TARGET ${target} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}")
    endif()
endfunction()

function(mulan_add_copy_files_target target)
    set(one_value_args DESTINATION COMMENT)
    set(multi_value_args FILES DEPENDS)
    cmake_parse_arguments(ARG "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_DESTINATION)
        message(FATAL_ERROR "mulan_add_copy_files_target requires DESTINATION")
    endif()

    set(copy_commands)
    foreach(file IN LISTS ARG_FILES)
        if(EXISTS "${file}" OR "${file}" MATCHES "^\\$<")
            list(APPEND copy_commands
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${file}" "${ARG_DESTINATION}"
            )
        endif()
    endforeach()

    if(NOT ARG_COMMENT)
        set(ARG_COMMENT "Copying runtime files for ${target}")
    endif()

    add_custom_target(${target}
        ${copy_commands}
        DEPENDS ${ARG_DEPENDS}
        COMMENT "${ARG_COMMENT}"
        COMMAND_EXPAND_LISTS
    )
endfunction()

function(mulan_copy_target_runtime_dlls_post_build target)
    if(NOT WIN32)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}>
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Syncing linked runtime DLLs..."
    )
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
