function(mulan_add_slang_shaders shader_target)
    set(options SPIRV DXIL DXBC GLSPIRV)
    set(one_value_args SOURCE_DIR OUTPUT_DIR)
    set(multi_value_args SOURCES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "mulan_add_slang_shaders requires SOURCE_DIR")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "mulan_add_slang_shaders requires OUTPUT_DIR")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "mulan_add_slang_shaders requires SOURCES")
    endif()

    if(NOT ARG_SPIRV AND NOT ARG_DXIL AND NOT ARG_DXBC AND NOT ARG_GLSPIRV)
        add_custom_target(${shader_target})
        message(STATUS "No rendering backend requires Slang shader output")
        return()
    endif()

    # Slang is a build tool rather than a runtime dependency. The Vulkan SDK
    # bundles slangc, but callers may pin a project-local compiler explicitly.
    set(MULAN_SLANGC_EXECUTABLE "${MULAN_SLANGC_EXECUTABLE}" CACHE FILEPATH
        "Path to the Slang shader compiler executable")

    # slangc 与 glslangValidator 由 vcpkg 提供（shader-slang、glslang[tools] 端口），
    # 作为构建工具安装在 ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools 下。
    # 本地若装有 Vulkan SDK（$VULKAN_SDK），仍作为回退来源使用。
    set(_mulan_shader_tool_hints)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _mulan_shader_tool_hints
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/shader-slang"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/glslang")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _mulan_shader_tool_hints
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/Bin32")
    endif()

    if(NOT MULAN_SLANGC_EXECUTABLE)
        find_program(_mulan_discovered_slangc
            NAMES slangc slangc.exe
            HINTS ${_mulan_shader_tool_hints}
            DOC "Discovered Slang shader compiler executable")
        if(_mulan_discovered_slangc)
            set(MULAN_SLANGC_EXECUTABLE "${_mulan_discovered_slangc}" CACHE FILEPATH
                "Path to the Slang shader compiler executable" FORCE)
        endif()
    endif()

    if(NOT MULAN_SLANGC_EXECUTABLE OR NOT EXISTS "${MULAN_SLANGC_EXECUTABLE}")
        message(FATAL_ERROR
            "A usable Slang compiler (slangc) was not found. Install a Vulkan SDK containing slangc, or configure "
            "-DMULAN_SLANGC_EXECUTABLE=<absolute path to slangc>.")
    endif()

    if(ARG_GLSPIRV)
        set(MULAN_GLSLANG_VALIDATOR_EXECUTABLE "${MULAN_GLSLANG_VALIDATOR_EXECUTABLE}" CACHE FILEPATH
            "Path to the glslangValidator executable used for OpenGL SPIR-V")
        if(NOT MULAN_GLSLANG_VALIDATOR_EXECUTABLE)
            find_program(_mulan_discovered_glslang_validator
                NAMES glslangValidator glslangValidator.exe
                HINTS ${_mulan_shader_tool_hints}
                DOC "Discovered GLSLang validator executable")
            if(_mulan_discovered_glslang_validator)
                set(MULAN_GLSLANG_VALIDATOR_EXECUTABLE "${_mulan_discovered_glslang_validator}" CACHE FILEPATH
                    "Path to the glslangValidator executable used for OpenGL SPIR-V" FORCE)
            endif()
        endif()

        if(NOT MULAN_GLSLANG_VALIDATOR_EXECUTABLE OR NOT EXISTS "${MULAN_GLSLANG_VALIDATOR_EXECUTABLE}")
            message(FATAL_ERROR
                "OpenGL shader output requires glslangValidator. Install a Vulkan SDK containing glslangValidator, or "
                "configure -DMULAN_GLSLANG_VALIDATOR_EXECUTABLE=<absolute path to glslangValidator>.")
        endif()
    endif()

    message(STATUS "Found slangc: ${MULAN_SLANGC_EXECUTABLE}")
    if(ARG_GLSPIRV)
        message(STATUS "Found glslangValidator: ${MULAN_GLSLANG_VALIDATOR_EXECUTABLE}")
    endif()

    # 调试信息：保留符号名与源码行号映射，供 RenderDoc / PIX 等 GPU 调试器使用。
    # 由 CMake option MULAN_SHADER_DEBUG_INFO（配置期决定，默认 OFF）控制，避免
    # 多配置生成器（如 Visual Studio）在 add_custom_command 中按 CONFIG 切换参数
    # 时展开为空串导致 slangc 报错。
    # - slangc：-g 补符号名；其 SPIR-V 后端不输出 OpLine（上游限制），DXIL 后端
    #   真正嵌入调试信息。
    # - glslangValidator：-g 把 GLSL 中的 #line 转成 SPIR-V OpLine，是 OpenGL 路径
    #   行号映射的关键。
    set(_mulan_slangc_debug_flags)
    set(_mulan_glslang_debug_flags)
    if(MULAN_SHADER_DEBUG_INFO)
        set(_mulan_slangc_debug_flags "-g")
        set(_mulan_glslang_debug_flags "-g")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
    file(GLOB shader_dependencies CONFIGURE_DEPENDS "${ARG_SOURCE_DIR}/*.slang")
    set(shader_outputs)

    foreach(shader_file IN LISTS ARG_SOURCES)
        if("${shader_file}" MATCHES "\\.vert\\.slang$")
            set(shader_stage vertex)
            set(glslang_stage vert)
            set(dxbc_profile vs_5_0)
            set(dxil_profile vs_6_0)
        elseif("${shader_file}" MATCHES "\\.frag\\.slang$")
            set(shader_stage fragment)
            set(glslang_stage frag)
            set(dxbc_profile ps_5_0)
            set(dxil_profile ps_6_0)
        else()
            message(FATAL_ERROR "Cannot infer Slang stage from shader file name: ${shader_file}")
        endif()

        set(shader_input "${ARG_SOURCE_DIR}/${shader_file}")

        if(ARG_SPIRV)
            string(REPLACE ".slang" ".spv" spirv_file "${shader_file}")
            set(spirv_output "${ARG_OUTPUT_DIR}/${spirv_file}")
            add_custom_command(
                OUTPUT "${spirv_output}"
                COMMAND "${MULAN_SLANGC_EXECUTABLE}"
                    -target spirv
                    -profile glsl_460
                    -entry main
                    -stage ${shader_stage}
                    -I "${ARG_SOURCE_DIR}"
                    ${_mulan_slangc_debug_flags}
                    -o "${spirv_output}"
                    "${shader_input}"
                DEPENDS ${shader_dependencies}
                COMMENT "Compiling Slang shader: ${shader_file} -> ${spirv_file} (Vulkan SPIR-V)"
                VERBATIM
            )
            list(APPEND shader_outputs "${spirv_output}")
        endif()

        if(ARG_DXBC)
            string(REPLACE ".slang" ".dxbc" dxbc_file "${shader_file}")
            set(dxbc_output "${ARG_OUTPUT_DIR}/${dxbc_file}")
            add_custom_command(
                OUTPUT "${dxbc_output}"
                COMMAND "${MULAN_SLANGC_EXECUTABLE}"
                    -target dxbc
                    -profile ${dxbc_profile}
                    -entry main
                    -I "${ARG_SOURCE_DIR}"
                    ${_mulan_slangc_debug_flags}
                    -o "${dxbc_output}"
                    "${shader_input}"
                DEPENDS ${shader_dependencies}
                COMMENT "Compiling Slang shader: ${shader_file} -> ${dxbc_file} (DX11 DXBC)"
                VERBATIM
            )
            list(APPEND shader_outputs "${dxbc_output}")
        endif()

        if(ARG_DXIL)
            string(REPLACE ".slang" ".dxil" dxil_file "${shader_file}")
            set(dxil_output "${ARG_OUTPUT_DIR}/${dxil_file}")
            add_custom_command(
                OUTPUT "${dxil_output}"
                COMMAND "${MULAN_SLANGC_EXECUTABLE}"
                    -target dxil
                    -profile ${dxil_profile}
                    -entry main
                    -I "${ARG_SOURCE_DIR}"
                    ${_mulan_slangc_debug_flags}
                    -o "${dxil_output}"
                    "${shader_input}"
                DEPENDS ${shader_dependencies}
                COMMENT "Compiling Slang shader: ${shader_file} -> ${dxil_file} (DX12 DXIL)"
                VERBATIM
            )
            list(APPEND shader_outputs "${dxil_output}")
        endif()

        if(ARG_GLSPIRV)
            string(REPLACE ".slang" ".glsl" glsl_file "${shader_file}")
            string(REPLACE ".slang" ".gl.spv" gl_spirv_file "${shader_file}")
            set(glsl_output "${ARG_OUTPUT_DIR}/${glsl_file}")
            set(gl_spirv_output "${ARG_OUTPUT_DIR}/${gl_spirv_file}")
            add_custom_command(
                OUTPUT "${gl_spirv_output}"
                BYPRODUCTS "${glsl_output}"
                COMMAND "${MULAN_SLANGC_EXECUTABLE}"
                    -target glsl
                    -profile glsl_460
                    -entry main
                    -stage ${shader_stage}
                    -DMULAN_OPENGL=1
                    -I "${ARG_SOURCE_DIR}"
                    ${_mulan_slangc_debug_flags}
                    -o "${glsl_output}"
                    "${shader_input}"
                COMMAND "${CMAKE_COMMAND}"
                    -DINPUT_FILE=${glsl_output}
                    -P "${CMAKE_SOURCE_DIR}/cmake/PatchOpenGLShaderBuiltins.cmake"
                COMMAND "${MULAN_GLSLANG_VALIDATOR_EXECUTABLE}"
                    -G
                    ${_mulan_glslang_debug_flags}
                    -S ${glslang_stage}
                    -e main
                    -o "${gl_spirv_output}"
                    "${glsl_output}"
                DEPENDS ${shader_dependencies} "${CMAKE_SOURCE_DIR}/cmake/PatchOpenGLShaderBuiltins.cmake"
                COMMENT "Compiling Slang shader: ${shader_file} -> ${gl_spirv_file} (OpenGL SPIR-V)"
                VERBATIM
            )
            list(APPEND shader_outputs "${gl_spirv_output}")
        endif()
    endforeach()

    add_custom_target(${shader_target} ALL DEPENDS ${shader_outputs})
endfunction()
