# Document - 实体-文档数据模型（静态库）
add_library(Document STATIC
    EntityId.cpp
    Entity.cpp
    EntityHandle.cpp
    Document.cpp
    OCCTShapeGeometry.cpp
    OCCTImporter.cpp
    ImporterFactory.cpp
    DocumentManager.cpp
)

# 构建时将头文件复制到 build/include/mulan/Document/...
file(GLOB_RECURSE DOCUMENT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${DOCUMENT_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/Document/${rel}" COPYONLY)
endforeach()

target_include_directories(Document PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# 依赖 Engine（Math 类型、Mesh、AABB）
target_link_libraries(Document PUBLIC Engine)

# OCCT 头文件路径（仅编译期需要）
target_include_directories(Document PRIVATE
    ${OCCT_INCLUDE_DIR}
)

# 链接 OCCT 库（OCCTShapeGeometry 实现）
foreach(lib ${OCCT_LIBS})
    find_library(OCCT_DOC_${lib} ${lib} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(OCCT_DOC_${lib})
        target_link_libraries(Document PRIVATE ${OCCT_DOC_${lib}})
    else()
        message(WARNING "OCCT library not found for Document: ${lib}")
    endif()
endforeach()

target_compile_definitions(Document PRIVATE
    BUILDING_DOCUMENT
    $<$<CONFIG:Debug>:_DEBUG>
)
