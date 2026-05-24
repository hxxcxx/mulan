# Engine - 渲染后端与场景管理（静态库）
add_library(Engine STATIC
    Platform/Win32Window.cpp
    RHI/DeviceFactory.cpp
    Scene/SceneNode.cpp
    Scene/Scene.cpp
    Scene/Frustum.cpp
    Scene/camera/Camera.cpp
    Scene/camera/TurntableRotation.cpp
    Scene/camera/TrackballRotation.cpp
    Render/SceneRenderer.cpp
    Render/EngineView.cpp
    Render/TextureLoader.cpp
    Render/TextureCache.cpp
    # Render passes
    Render/pass/ForwardPass.cpp
    # Text rendering (MSDF)
    Render/text/FontAtlas.cpp
    Render/text/TextLayout.cpp
    Render/text/TextRenderer.cpp
    # Material system
    Render/material/MaterialCache.cpp
    Render/material/MaterialInstance.cpp
    Render/material/MaterialSerializer.cpp
    Render/material/MaterialLibrary.cpp
    # Vulkan backend
    RHI/vulkan/VKBuffer.cpp
    RHI/vulkan/VKTexture.cpp
    RHI/vulkan/VKShader.cpp
    RHI/vulkan/VKFence.cpp
    RHI/vulkan/VKFrameContext.cpp
    RHI/vulkan/VKCommandList.cpp
    RHI/vulkan/VKPipelineState.cpp
    RHI/vulkan/VKDescriptorAllocator.cpp
    RHI/vulkan/VKUploadContext.cpp
    RHI/vulkan/VKSwapChain.cpp
    RHI/vulkan/VKRenderTarget.cpp
    RHI/vulkan/VKDevice.cpp
    RHI/vulkan/VKDeviceInit.cpp
    RHI/vulkan/VKSampler.cpp
    # D3D12 backend
    RHI/d3d12/DX12Fence.cpp
    RHI/d3d12/DX12DescriptorAllocator.cpp
    RHI/d3d12/DX12Buffer.cpp
    RHI/d3d12/DX12UploadContext.cpp
    RHI/d3d12/DX12Texture.cpp
    RHI/d3d12/DX12Shader.cpp
    RHI/d3d12/DX12PipelineState.cpp
    RHI/d3d12/DX12CommandList.cpp
    RHI/d3d12/DX12SwapChain.cpp
    RHI/d3d12/DX12RenderTarget.cpp
    RHI/d3d12/DX12FrameContext.cpp
    RHI/d3d12/DX12Device.cpp
    RHI/d3d12/DX12Sampler.cpp
    # D3D11 backend
    RHI/d3d11/DX11Buffer.cpp
    RHI/d3d11/DX11Texture.cpp
    RHI/d3d11/DX11Shader.cpp
    RHI/d3d11/DX11Fence.cpp
    RHI/d3d11/DX11PipelineState.cpp
    RHI/d3d11/DX11CommandList.cpp
    RHI/d3d11/DX11SwapChain.cpp
    RHI/d3d11/DX11RenderTarget.cpp
    RHI/d3d11/DX11Device.cpp
    RHI/d3d11/DX11Sampler.cpp
    # OpenGL backend (placeholder)
    RHI/opengl/GLDevice.cpp
    RHI/opengl/GLShader.cpp
    RHI/opengl/GLSwapChain.cpp
    RHI/opengl/GLPipelineState.cpp
    RHI/opengl/GLBuffer.cpp
    RHI/opengl/GLCommandList.cpp
    RHI/opengl/GLRenderTarget.cpp
    RHI/opengl/GLTexture.cpp
    RHI/opengl/GLSampler.cpp
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

# --- OpenGL + GLAD (vcpkg) ---
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)

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
    OpenGL::GL
    glad::glad
    nlohmann_json::nlohmann_json
    msdfgen::msdfgen
    msdf-atlas-gen
    d3d12
    d3d11
    dxgi
)

target_compile_definitions(Engine PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

# MSVC: 多文件并行编译写同一 PDB 需要 /FS
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

# 查找 fxc (Windows SDK, 用于 DXBC/SM5.0)
find_program(FXC_EXECUTABLE fxc
    HINTS
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
)
if(FXC_EXECUTABLE)
    message(STATUS "Found fxc: ${FXC_EXECUTABLE}")
else()
    message(WARNING "fxc not found — DXBC shaders (D3D11) will not be compiled.")
endif()

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

    # Common.hlsli 作为依赖（不单独编译）
    set(SHADER_COMMON "${SHADER_SOURCE_DIR}/Common.hlsli")

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

        # --- DXBC (D3D11, SM 5.0 via fxc) ---
        if(FXC_EXECUTABLE)
            string(REPLACE ".hlsl" ".dxbc" DXBC_FILE "${SHADER_FILE}")
            set(DXBC_OUTPUT "${SHADER_OUTPUT_DIR}/${DXBC_FILE}")

            add_custom_command(
                OUTPUT  "${DXBC_OUTPUT}"
                COMMAND "${FXC_EXECUTABLE}"
                    /T ${SHADER_STAGE}_5_0
                    /E ${SHADER_ENTRY}
                    /I "${SHADER_SOURCE_DIR}"
                    /Fo "${DXBC_OUTPUT}"
                    /nologo
                    "${SHADER_INPUT}"
                DEPENDS "${SHADER_INPUT}" "${SHADER_COMMON}"
                COMMENT "Compiling shader: ${SHADER_FILE} → ${DXBC_FILE} (DXBC/SM5.0 via fxc)"
            )
            list(APPEND SHADER_OUTPUTS "${DXBC_OUTPUT}")
        endif()
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
