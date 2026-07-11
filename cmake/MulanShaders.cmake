function(mulan_add_hlsl_shaders shader_target)
    set(options SPIRV DXIL)
    set(one_value_args SOURCE_DIR OUTPUT_DIR)
    set(multi_value_args SOURCES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "mulan_add_hlsl_shaders requires SOURCE_DIR")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "mulan_add_hlsl_shaders requires OUTPUT_DIR")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "mulan_add_hlsl_shaders requires SOURCES")
    endif()

    if(NOT ARG_SPIRV AND NOT ARG_DXIL)
        add_custom_target(${shader_target})
        message(STATUS "No rendering backend requires HLSL shader output")
        return()
    endif()

    # DXC is a build tool, not a runtime dependency. Building the vcpkg
    # directx-dxc port pulls in LLVM/tablegen and is unnecessarily expensive
    # for CI. Allow callers to pin a tool explicitly, retain compatibility
    # with an already-installed vcpkg package, then discover SDK installations.
    set(MULAN_DXC_EXECUTABLE "${MULAN_DXC_EXECUTABLE}" CACHE FILEPATH
        "Path to the DirectX Shader Compiler executable")

    if(NOT MULAN_DXC_EXECUTABLE)
        find_package(directx-dxc CONFIG QUIET)
        if(DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
            set(MULAN_DXC_EXECUTABLE "${DIRECTX_DXC_TOOL}")
        endif()
    endif()

    if(NOT MULAN_DXC_EXECUTABLE)
        set(_mulan_dxc_hints)
        if(DEFINED ENV{VULKAN_SDK})
            list(APPEND _mulan_dxc_hints
                "$ENV{VULKAN_SDK}/Bin"
                "$ENV{VULKAN_SDK}/Bin32")
        endif()
        if(WIN32)
            file(GLOB _mulan_windows_sdk_bins LIST_DIRECTORIES TRUE
                "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
            list(SORT _mulan_windows_sdk_bins COMPARE NATURAL ORDER DESCENDING)
            list(APPEND _mulan_dxc_hints ${_mulan_windows_sdk_bins})
        endif()
        find_program(_mulan_discovered_dxc
            NAMES dxc dxc.exe
            HINTS ${_mulan_dxc_hints}
            DOC "Discovered DirectX Shader Compiler executable")
        if(_mulan_discovered_dxc)
            set(MULAN_DXC_EXECUTABLE "${_mulan_discovered_dxc}" CACHE FILEPATH
                "Path to the DirectX Shader Compiler executable" FORCE)
        endif()
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    if(NOT MULAN_DXC_EXECUTABLE OR NOT EXISTS "${MULAN_DXC_EXECUTABLE}")
        message(FATAL_ERROR
            "A usable DirectX Shader Compiler (dxc) was not found. Install a "
            "Windows or Vulkan SDK containing dxc, or configure with "
            "-DMULAN_DXC_EXECUTABLE=<absolute path to dxc>."
        )
    endif()

    message(STATUS "Found dxc: ${MULAN_DXC_EXECUTABLE}")

    set(shader_common "${ARG_SOURCE_DIR}/common.hlsli")
    set(ibl_common "${ARG_SOURCE_DIR}/ibl_common.hlsli")
    set(shader_outputs)

    foreach(shader_file IN LISTS ARG_SOURCES)
        if("${shader_file}" MATCHES "\\.vert\\.hlsl$")
            set(shader_stage vs)
        elseif("${shader_file}" MATCHES "\\.frag\\.hlsl$")
            set(shader_stage ps)
        else()
            message(FATAL_ERROR "Cannot infer HLSL stage from shader file name: ${shader_file}")
        endif()
        set(shader_entry main)

        set(shader_input "${ARG_SOURCE_DIR}/${shader_file}")
        set(shader_depends "${shader_input}")
        if("${shader_file}" MATCHES "^ibl_.*\\.frag\\.hlsl$")
            list(APPEND shader_depends "${ibl_common}")
        elseif(NOT "${shader_file}" STREQUAL "ibl.vert.hlsl")
            list(APPEND shader_depends "${shader_common}")
        endif()
        if("${shader_file}" STREQUAL "pbr_tangent.frag.hlsl")
            list(APPEND shader_depends "${ARG_SOURCE_DIR}/pbr.frag.hlsl")
        endif()

        if(ARG_SPIRV)
            string(REPLACE ".hlsl" ".spv" spv_file "${shader_file}")
            set(spv_output "${ARG_OUTPUT_DIR}/${spv_file}")
            add_custom_command(
                OUTPUT "${spv_output}"
                COMMAND "${MULAN_DXC_EXECUTABLE}"
                    -T ${shader_stage}_6_0
                    -E ${shader_entry}
                    -spirv
                    -I "${ARG_SOURCE_DIR}"
                    -Fo "${spv_output}"
                    "${shader_input}"
                DEPENDS ${shader_depends}
                COMMENT "Compiling shader: ${shader_file} -> ${spv_file} (SPIR-V)"
            )
            list(APPEND shader_outputs "${spv_output}")
        endif()

        if(ARG_DXIL)
            string(REPLACE ".hlsl" ".dxil" dxil_file "${shader_file}")
            set(dxil_output "${ARG_OUTPUT_DIR}/${dxil_file}")
            add_custom_command(
                OUTPUT "${dxil_output}"
                COMMAND "${MULAN_DXC_EXECUTABLE}"
                    -T ${shader_stage}_6_0
                    -E ${shader_entry}
                    -I "${ARG_SOURCE_DIR}"
                    -Fo "${dxil_output}"
                    "${shader_input}"
                DEPENDS ${shader_depends}
                COMMENT "Compiling shader: ${shader_file} -> ${dxil_file} (DXIL)"
            )
            list(APPEND shader_outputs "${dxil_output}")
        endif()
    endforeach()

    add_custom_target(${shader_target} ALL DEPENDS ${shader_outputs})
endfunction()
