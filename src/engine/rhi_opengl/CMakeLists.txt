# rhi_opengl - Windows WGL / Linux GLX 原生 OpenGL RHI 后端。
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

set(MULAN_OPENGL_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND MULAN_OPENGL_PLATFORM_SOURCES platform/gl_context_wgl.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    list(APPEND MULAN_OPENGL_PLATFORM_SOURCES platform/gl_context_glx.cpp)
else()
    message(FATAL_ERROR "The native OpenGL backend has no context implementation for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(rhi_opengl STATIC
    backend_register.cpp
    ${MULAN_OPENGL_PLATFORM_SOURCES}
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

mulan_copy_public_headers(rhi_opengl rhi_opengl backend.h)

target_link_libraries(rhi_opengl PRIVATE rhi glad::glad)

if(WIN32)
    target_link_libraries(rhi_opengl PRIVATE OpenGL::GL)
else()
    target_link_libraries(rhi_opengl PRIVATE OpenGL::GLX X11::X11)
endif()

target_precompile_headers(rhi_opengl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../pch.h
)

set_target_properties(rhi_opengl PROPERTIES OUTPUT_NAME mulan_rhi_opengl)
target_compile_definitions(rhi_opengl PRIVATE $<$<CONFIG:Debug>:_DEBUG> _USE_MATH_DEFINES)

if(MSVC)
    target_compile_options(rhi_opengl PRIVATE /FS)
endif()
