# Command - 命令体系（静态库）
#
# 命令注册、执行、历史记录。
# 依赖 IO（导入器）、World（数据模型），不依赖 Qt。
add_library(Command STATIC
    command_manager.cpp
)

# 构建时将头文件复制到 build/include/mulan/command/...
file(GLOB_RECURSE COMMAND_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${COMMAND_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/command/${rel}" COPYONLY)
endforeach()

target_include_directories(Command PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(Command PUBLIC IO)

set_target_properties(Command PROPERTIES OUTPUT_NAME mulan_command)

target_compile_definitions(Command PRIVATE
    BUILDING_COMMAND
    $<$<CONFIG:Debug>:_DEBUG>
)
