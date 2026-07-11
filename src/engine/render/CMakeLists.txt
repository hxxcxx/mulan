# render - 渲染管线：前端(frontend) + 后端(backend) + 各阶段(stages) + 文本/材质/overlay。
# 对外暴露模块，view/app 消费。
add_library(render STATIC
    camera/camera.cpp
    camera/turntable_rotation.cpp
    camera/trackball_rotation.cpp
    asset_gpu_registry.cpp
    environment_map.cpp
    mesh_draw_command.cpp
    technique/render_technique.cpp
    # Render frontend / backend
    frontend/render_contract.cpp
    frontend/render_world.cpp
    frontend/render_workload.cpp
    backend/render_compiler.cpp
    backend/render_renderer.cpp
    # Draw executors / stages
    draw/geometry_draw_shared_resources.cpp
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

mulan_copy_public_headers(render render)

find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(render
    PUBLIC
        rhi
        core
        math
        graphics
    PRIVATE
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

# ── HLSL shaders ──
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")
set(SHADER_OUTPUT_DIR "${MULAN_SHADER_OUTPUT_DIR}")
set(SHADER_SOURCES
    solid.vert.hlsl
    solid.frag.hlsl
    albedo.vert.hlsl
    albedo.frag.hlsl
    pbr.vert.hlsl
    pbr.frag.hlsl
    pbr_tangent.vert.hlsl
    pbr_tangent.frag.hlsl
    wireframe.vert.hlsl
    wireframe.frag.hlsl
    edge.vert.hlsl
    edge.frag.hlsl
    highlight_surface.frag.hlsl
    highlight_edge.frag.hlsl
    viewcube.frag.hlsl
    viewcube_line.frag.hlsl
    text.vert.hlsl
    text.frag.hlsl
    ibl.vert.hlsl
    ibl_irradiance.frag.hlsl
    ibl_prefilter.frag.hlsl
    ibl_brdf_lut.frag.hlsl
)

mulan_add_hlsl_shaders(mulan_shaders
    SOURCE_DIR "${SHADER_SOURCE_DIR}"
    OUTPUT_DIR "${SHADER_OUTPUT_DIR}"
    SOURCES ${SHADER_SOURCES}
)
add_dependencies(render mulan_shaders)

target_compile_definitions(render PRIVATE
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
)
