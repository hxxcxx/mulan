# Document - 真实数据源层（静态库）
#
# 持有 B-Rep (OCCT TopoDS_Shape) + 拥有 World。
# 是文件的"真理之源"表示：导入/编辑/保存的统一落点。
# World 不感知 OCCT，OCCT 依赖只在本模块。
add_library(Document STATIC
    Document.cpp
    SolidGeometryData.cpp
)

# 构建时将头文件复制到 build/include/mulan/document/...
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

# 依赖 World（拥有 + GeometryData 多态基类）— PUBLIC：下游访问 world() 需要
target_link_libraries(Document PUBLIC World)

# OCCT 头文件和库（仅 SolidGeometryData 编译期需要，PRIVATE 不外泄）
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

target_compile_definitions(Document PRIVATE
    BUILDING_DOCUMENT
    $<$<CONFIG:Debug>:_DEBUG>
)
