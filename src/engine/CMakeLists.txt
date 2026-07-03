# Engine - 渲染后端与场景管理（静态库）
add_library(Engine STATIC
    platform/win32_window.cpp
    rhi/device_factory.cpp
    scene/camera/camera.cpp
    scene/camera/turntable_rotation.cpp
    scene/camera/trackball_rotation.cpp
    render/texture_loader.cpp
    render/texture_cache.cpp
    render/render_resource_cache.cpp
    render/mesh_draw_command.cpp
    # RenderGraph
    render/graph/forward_pass.cpp
    render/graph/edge_pass.cpp
    # Text rendering (MSDF)
    render/text/font_atlas.cpp
    render/text/text_layout.cpp
    render/text/font_manager.cpp
    # ViewCube navigation
    render/viewcube/view_cube_renderer.cpp
    # Material system
    render/material/material_cache.cpp
    render/material/material_instance.cpp
    render/material/material_serializer.cpp
    render/material/material_library.cpp
    # Vulkan backend
    rhi/vulkan/vk_buffer.cpp
    rhi/vulkan/vk_texture.cpp
    rhi/vulkan/vk_shader.cpp
    rhi/vulkan/vk_fence.cpp
    rhi/vulkan/vk_frame_context.cpp
    rhi/vulkan/vk_command_list.cpp
    rhi/vulkan/vk_pipeline_state.cpp
    rhi/vulkan/vk_descriptor_allocator.cpp
    rhi/vulkan/vk_upload_context.cpp
    rhi/vulkan/vk_swap_chain.cpp
    rhi/vulkan/vk_render_target.cpp
    rhi/vulkan/vk_device.cpp
    rhi/vulkan/vk_device_init.cpp
    rhi/vulkan/vk_sampler.cpp
    # D3D12 backend
    rhi/d3d12/dx12_fence.cpp
    rhi/d3d12/dx12_descriptor_allocator.cpp
    rhi/d3d12/dx12_buffer.cpp
    rhi/d3d12/dx12_upload_context.cpp
    rhi/d3d12/dx12_texture.cpp
    rhi/d3d12/dx12_shader.cpp
    rhi/d3d12/dx12_pipeline_state.cpp
    rhi/d3d12/dx12_command_list.cpp
    rhi/d3d12/dx12_swap_chain.cpp
    rhi/d3d12/dx12_render_target.cpp
    rhi/d3d12/dx12_frame_context.cpp
    rhi/d3d12/dx12_device.cpp
    rhi/d3d12/dx12_sampler.cpp
)

# 构建时将头文件复制到 build/include/mulan/engine/...
file(GLOB_RECURSE ENGINE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${ENGINE_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/engine/${rel}" COPYONLY)
endforeach()

target_include_directories(Engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# --- GLM (header-only math library) ---
find_package(glm CONFIG REQUIRED)

# --- Vulkan ---
find_package(Vulkan REQUIRED)

# VMA (vulkan-memory-allocator)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# nlohmann-json
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(Engine PUBLIC
    Core
    glm::glm
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    nlohmann_json::nlohmann_json
    msdfgen::msdfgen
    msdf-atlas-gen
    d3d12
    dxgi
)

target_compile_definitions(Engine PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

# MSVC: 多文件并行编译写同一 PDB 需要 /FS
set_target_properties(Engine PROPERTIES OUTPUT_NAME mulan_engine)

if(MSVC)
    target_compile_options(Engine PRIVATE /FS)
endif()

# --- Shader 编译 (HLSL → SPIR-V + DXIL via dxc, DXBC via fxc) ---
# 查找 dxc
find_program(DXC_EXECUTABLE dxc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "${Vulkan_INSTALL_DIR}/Bin"
        "C:/VulkanSDK/*/Bin"
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

if(DXC_EXECUTABLE)
    message(STATUS "Found dxc: ${DXC_EXECUTABLE}")

    set(SHADER_SOURCES
        solid.vert.hlsl
        solid.frag.hlsl
        wireframe.vert.hlsl
        wireframe.frag.hlsl
        pick.vert.hlsl
        pick.frag.hlsl
        edge.vert.hlsl
        edge.frag.hlsl
        text.vert.hlsl
        text.frag.hlsl
    )

    # common.hlsli 作为依赖（不单独编译）
    set(SHADER_COMMON "${SHADER_SOURCE_DIR}/common.hlsli")

    # 确定每个 shader 文件的 stage 和入口点
    set(SOLID_VERT_STAGE    vs)
    set(SOLID_VERT_ENTRY    main)
    set(SOLID_FRAG_STAGE    ps)
    set(SOLID_FRAG_ENTRY    main)
    set(WIREFRAME_VERT_STAGE vs)
    set(WIREFRAME_VERT_ENTRY main)
    set(WIREFRAME_FRAG_STAGE ps)
    set(WIREFRAME_FRAG_ENTRY main)
    set(PICK_VERT_STAGE     vs)
    set(PICK_VERT_ENTRY     main)
    set(PICK_FRAG_STAGE     ps)
    set(PICK_FRAG_ENTRY     main)
    set(EDGE_VERT_STAGE     vs)
    set(EDGE_VERT_ENTRY     main)
    set(EDGE_FRAG_STAGE     ps)
    set(EDGE_FRAG_ENTRY     main)
    set(TEXT_VERT_STAGE     vs)
    set(TEXT_VERT_ENTRY     main)
    set(TEXT_FRAG_STAGE     ps)
    set(TEXT_FRAG_ENTRY     main)

    foreach(SHADER_FILE ${SHADER_SOURCES})
        # 转为大写标识符用于变量查找
        string(REPLACE "." "_" SHADER_ID "${SHADER_FILE}")
        string(TOUPPER "${SHADER_ID}" SHADER_ID_UPPER)

        # 获取 stage 和 entry
        set(SHADER_STAGE ${${SHADER_ID_UPPER}_STAGE})
        set(SHADER_ENTRY ${${SHADER_ID_UPPER}_ENTRY})

        if(NOT SHADER_STAGE)
            # 从文件名推断
            if("${SHADER_FILE}" MATCHES "\\.vert\\.")
                set(SHADER_STAGE vs)
            elseif("${SHADER_FILE}" MATCHES "\\.frag\\.")
                set(SHADER_STAGE ps)
            endif()
            set(SHADER_ENTRY main)
        endif()

        set(SHADER_INPUT "${SHADER_SOURCE_DIR}/${SHADER_FILE}")

        # --- SPIR-V (Vulkan) ---
        string(REPLACE ".hlsl" ".spv" SPV_FILE "${SHADER_FILE}")
        set(SPV_OUTPUT "${SHADER_OUTPUT_DIR}/${SPV_FILE}")

        add_custom_command(
            OUTPUT  "${SPV_OUTPUT}"
            COMMAND "${DXC_EXECUTABLE}"
                -T ${SHADER_STAGE}_6_0
                -E ${SHADER_ENTRY}
                -spirv
                -I "${SHADER_SOURCE_DIR}"
                -Fo "${SPV_OUTPUT}"
                "${SHADER_INPUT}"
            DEPENDS "${SHADER_INPUT}" "${SHADER_COMMON}"
            COMMENT "Compiling shader: ${SHADER_FILE} → ${SPV_FILE} (SPIR-V)"
        )
        list(APPEND SHADER_OUTPUTS "${SPV_OUTPUT}")

        # --- DXIL (D3D12) ---
        string(REPLACE ".hlsl" ".dxil" DXIL_FILE "${SHADER_FILE}")
        set(DXIL_OUTPUT "${SHADER_OUTPUT_DIR}/${DXIL_FILE}")

        add_custom_command(
            OUTPUT  "${DXIL_OUTPUT}"
            COMMAND "${DXC_EXECUTABLE}"
                -T ${SHADER_STAGE}_6_0
                -E ${SHADER_ENTRY}
                -I "${SHADER_SOURCE_DIR}"
                -Fo "${DXIL_OUTPUT}"
                "${SHADER_INPUT}"
            DEPENDS "${SHADER_INPUT}" "${SHADER_COMMON}"
            COMMENT "Compiling shader: ${SHADER_FILE} → ${DXIL_FILE} (DXIL)"
        )
        list(APPEND SHADER_OUTPUTS "${DXIL_OUTPUT}")
    endforeach()

    add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(Engine Shaders)

    # 将 shader 输出目录作为 include path（方便运行时加载）
    target_compile_definitions(Engine PRIVATE
        SHADER_DIR="${SHADER_OUTPUT_DIR}"
    )
else()
    message(WARNING "dxc not found — shaders will not be compiled. Set VULKAN_SDK or install dxc.")
endif()
