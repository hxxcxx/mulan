# Slang 的 GLSL 后端会把 SV_InstanceID 输出成 Vulkan GLSL 名称
# `gl_InstanceIndex - gl_BaseInstance`，而 OpenGL GLSL 的等价内建量是
# 已排除 base-instance 的 `gl_InstanceID`。在 glslang -G 前做精确替换；
# 不包含该表达式的 shader 内容保持不变。

if(NOT DEFINED INPUT_FILE OR NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "INPUT_FILE must name an existing generated GLSL file")
endif()

file(READ "${INPUT_FILE}" shader_source)
string(REPLACE "gl_InstanceIndex - gl_BaseInstance" "gl_InstanceID" shader_source "${shader_source}")
file(WRITE "${INPUT_FILE}" "${shader_source}")
