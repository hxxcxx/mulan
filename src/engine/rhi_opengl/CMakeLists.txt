# rhi_opengl - Windows native OpenGL RHI backend.
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(rhi_opengl STATIC
    backend_register.cpp
    platform/gl_context_wgl.cpp
    gl_buffer.cpp
    gl_transient_uniform_arena.cpp
    gl_bind_group.cpp
    gl_command_list.cpp
    gl_device.cpp
    gl_fence.cpp
    gl_pipeline_state.cpp
    gl_render_target.cpp
    gl_sampler.cpp
    gl_shader.cpp
    gl_swap_chain.cpp
    gl_texture.cpp
)

mulan_copy_public_headers(rhi_opengl rhi_opengl)

target_link_libraries(rhi_opengl
    PRIVATE rhi glad::glad OpenGL::GL
)

target_precompile_headers(rhi_opengl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(rhi_opengl PROPERTIES OUTPUT_NAME mulan_rhi_opengl)
target_compile_definitions(rhi_opengl PRIVATE $<$<CONFIG:Debug>:_DEBUG> _USE_MATH_DEFINES)

if(MSVC)
    target_compile_options(rhi_opengl PRIVATE /FS)
endif()
