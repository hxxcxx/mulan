# render - 渲染管线：前端(frontend) + 后端(backend) + 各阶段(stages) + 文本/材质/overlay。
# 对外暴露模块，view/app 消费。
add_library(render STATIC
    asset_gpu_key.cpp
    asset_gpu_registry.cpp
    texture_mip_chain.cpp
    device_resource_service.cpp
    device_pipeline_library.cpp
    environment_map.cpp
    mesh_draw_command.cpp
    technique/render_technique.cpp
    # Render frontend / backend
    frontend/render_contract.cpp
    frontend/render_world.cpp
    frontend/render_workload.cpp
    overlay/view_cube_contract.cpp
    backend/render_compiler.cpp
    backend/detail/render_packet_cache.cpp
    backend/detail/render_visibility_index.cpp
    backend/detail/draw_command_assembler.cpp
    backend/forward_renderer.cpp
    # Render runtime
    runtime/render_device_context.cpp
    runtime/render_thread.cpp
    runtime/render_executor.cpp
    runtime/render_session.cpp
    runtime/present_surface.cpp
    runtime/capture_target.cpp
    runtime/render_channel.cpp
    # Draw executors / stages
    draw/draw_fallback_resources.cpp
    draw/geometry_draw_shared_resources.cpp
    draw/geometry_draw_batch.cpp
    draw/geometry_draw_executor.cpp
    forward/face_stage.cpp
    forward/edge_stage.cpp
    forward/highlight_stage.cpp
    # Text rendering (MSDF)
    text/font_atlas.cpp
    text/font_atlas_cache.cpp
    text/text_layout.cpp
    text/font_manager.cpp
    text/text_stage.cpp
    # Overlay / navigation stages
    overlay/view_cube_stage.cpp
    # Material system
    material/material_cache.cpp
)

mulan_copy_public_headers(render render
    asset_gpu_key.h
    light_environment.h
    material/material.h
    overlay/view_cube_contract.h
    frontend/display_mode.h
    frontend/pick_identity.h
    frontend/selection_visual_state.h
    frontend/render_handle.h
    frontend/render_record_store.h
    frontend/render_object.h
    frontend/render_world_snapshot.h
    frontend/render_world.h
    frontend/render_resource_prepare.h
    frontend/render_view_desc.h
    frontend/render_request.h
    frontend/render_contract.h
    frontend/render_capture.h
    frontend/render_frame_submission.h
    runtime/render_config.h
    runtime/render_surface_state.h
    runtime/render_session.h
)

find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(render
    PUBLIC
        core
        math
        graphics
    PRIVATE
        rhi
        mulan::profiling
        nlohmann_json::nlohmann_json
        msdfgen::msdfgen
        msdf-atlas-gen
)

target_precompile_headers(render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(render PROPERTIES OUTPUT_NAME mulan_render)

target_compile_definitions(render PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    _USE_MATH_DEFINES
)

if(MSVC)
    target_compile_options(render PRIVATE /FS)
endif()

# ── Slang shaders ──
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")
set(SHADER_OUTPUT_DIR "${MULAN_SHADER_OUTPUT_DIR}")
set(SHADER_SOURCES
    solid.vert.slang
    unlit.frag.slang
    legacy.frag.slang
    legacy_tangent.frag.slang
    pbr.vert.slang
    pbr.frag.slang
    pbr_tangent.vert.slang
    pbr_instanced.vert.slang
    pbr_tangent_instanced.vert.slang
    edge_instanced.vert.slang
    pbr_tangent.frag.slang
    wireframe.vert.slang
    wireframe.frag.slang
    edge.vert.slang
    edge.frag.slang
    highlight_surface.frag.slang
    highlight_edge.frag.slang
    viewcube.frag.slang
    viewcube.vert.slang
    viewcube_line.frag.slang
    text.vert.slang
    text.frag.slang
    ibl.vert.slang
    ibl_irradiance.frag.slang
    ibl_prefilter.frag.slang
    ibl_brdf_lut.frag.slang
)

set(MULAN_SHADER_OUTPUT_FORMATS)
if(MULAN_ENABLE_VULKAN_BACKEND)
    list(APPEND MULAN_SHADER_OUTPUT_FORMATS SPIRV)
endif()
if(MULAN_ENABLE_D3D11_BACKEND AND WIN32)
    list(APPEND MULAN_SHADER_OUTPUT_FORMATS DXBC)
endif()
if(MULAN_ENABLE_D3D12_BACKEND AND WIN32)
    list(APPEND MULAN_SHADER_OUTPUT_FORMATS DXIL)
endif()
if(MULAN_ENABLE_OPENGL_BACKEND)
    list(APPEND MULAN_SHADER_OUTPUT_FORMATS GLSPIRV)
endif()

mulan_add_slang_shaders(mulan_shaders
    ${MULAN_SHADER_OUTPUT_FORMATS}
    SOURCE_DIR "${SHADER_SOURCE_DIR}"
    OUTPUT_DIR "${SHADER_OUTPUT_DIR}"
    SOURCES ${SHADER_SOURCES}
)
add_dependencies(render mulan_shaders)

target_compile_definitions(render PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
)
