# IO - 文件 I/O 与导入器
add_library(IO STATIC
    occt_importer.cpp
    assimp_importer.cpp
    gltf_importer.cpp
    importer_registry.cpp
    importer_factory.cpp
    file_manager.cpp
    mesh_import_builder.cpp
    document.cpp
    shape_render_geometry.cpp
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

# fastgltf header-only: vcpkg 的 include 路径
target_include_directories(IO PRIVATE
    ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include
)

find_package(assimp CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

# 依赖 Core、Engine、Scene、Asset（Document 使用）
target_link_libraries(IO PUBLIC Core Engine Scene Asset
                      PRIVATE assimp::assimp fastgltf::fastgltf)

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
    FASTGLTF_USE_NLOHMANN_JSON
    $<$<CONFIG:Debug>:_DEBUG>
)
