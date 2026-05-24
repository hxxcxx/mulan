# BRep — B-Rep 拓扑模块（DLL）
#
# 当前只含拓扑结构类（Vertex/Edge/Wire/Face/Shell/Solid + 压缩序列化）
# 后续扩展：Curve/Surface variant、Builder、布尔运算等
#
# 依赖: Geometry (几何内核)

set(BREP_SOURCES
    BRep.cpp
)

add_library(BRep SHARED
    ${BREP_SOURCES}
    # 后续添加 .cpp 文件
)

target_include_directories(BRep PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(BRep PUBLIC Geometry)

target_compile_definitions(BRep PRIVATE BUILDING_BREP=1)

if(MSVC)
    target_compile_options(BRep PRIVATE /FS)
endif()

# GLM experimental extensions (gtx/norm etc.)
target_compile_definitions(BRep PUBLIC GLM_ENABLE_EXPERIMENTAL)

# 复制头文件到构建目录
file(GLOB_RECURSE BREP_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
foreach(hdr ${BREP_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/brep/${rel}" COPYONLY)
endforeach()
