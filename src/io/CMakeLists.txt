# IO - 文件I/O + 导入器（静态库）
add_library(IO STATIC
    occt_importer.cpp
    importer_factory.cpp
    file_manager.cpp
)

# 构建时将头文件复制到 build/include/mulan/io/...
file(GLOB_RECURSE IO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${IO_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/io/${rel}" COPYONLY)
endforeach()

target_include_directories(IO PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# 依赖 Engine（Math 类型、Mesh、AABB）+ Document（Importer 填充 Document）
target_link_libraries(IO PUBLIC Engine Document)

# OCCT 头文件路径（仅编译期需要）
target_include_directories(IO PRIVATE
    ${OCCT_INCLUDE_DIR}
)

# 链接 OCCT 库
foreach(lib ${OCCT_LIBS})
    find_library(OCCT_IO_${lib} ${lib} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(OCCT_IO_${lib})
        target_link_libraries(IO PRIVATE ${OCCT_IO_${lib}})
    else()
        message(WARNING "OCCT library not found for IO: ${lib}")
    endif()
endforeach()

set_target_properties(IO PROPERTIES OUTPUT_NAME mulan_io)

target_compile_definitions(IO PRIVATE
    BUILDING_IO
    $<$<CONFIG:Debug>:_DEBUG>
)
