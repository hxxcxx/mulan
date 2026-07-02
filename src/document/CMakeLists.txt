# Document - 导入模型、资源库和场景实例的数据入口
add_library(Document STATIC
    document.cpp
    shape_render_geometry.cpp
)

file(GLOB_RECURSE DOCUMENT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${DOCUMENT_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/document/${rel}" COPYONLY)
endforeach()

target_include_directories(Document PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(Document
    PUBLIC
        Scene
        Asset
)

target_include_directories(Document PRIVATE
    ${OCCT_INCLUDE_DIR}
)

foreach(lib ${OCCT_LIBS})
    find_library(DOCUMENT_OCCT_${lib} ${lib} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(DOCUMENT_OCCT_${lib})
        target_link_libraries(Document PRIVATE ${DOCUMENT_OCCT_${lib}})
    else()
        message(WARNING "OCCT library not found for Document: ${lib}")
    endif()
endforeach()

set_target_properties(Document PROPERTIES OUTPUT_NAME mulan_document)

target_compile_definitions(Document PRIVATE
    BUILDING_DOCUMENT
    $<$<CONFIG:Debug>:_DEBUG>
)
