# Modeling - 几何建模抽象层（DLL）
#
# 提供内核无关的 Shape 抽象 + 可插拔的内核实现。
# 外部只依赖 Modeling 公共头文件，不感知 OCCT 等内核。

# ── 公共头文件（接口层，不含内核实现）──
set(MODELING_PUBLIC_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/ModelingExport.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shape.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShapeImpl.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShapeFactory.h"
)

# ── 内核无关实现 ──
set(MODELING_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ShapeFactory.cpp"
)

# ── OCCT 后端 ──
set(OCCT_SHAPE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/OCCT/OCCTShape.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/OCCT/OCCTShape.cpp"
)

add_library(Modeling SHARED
    ${MODELING_PUBLIC_HEADERS}
    ${MODELING_SOURCES}
    ${OCCT_SHAPE_SOURCES}
)

# 构建时将公共头文件复制到 build/include/mulan/Modeling/...
foreach(hdr ${MODELING_PUBLIC_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/Modeling/${rel}" COPYONLY)
endforeach()

# OCCTShape.h 也复制（供需要知道具体类型的地方使用）
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/OCCT/OCCTShape.h"
    "${CMAKE_BINARY_DIR}/include/mulan/Modeling/OCCT/OCCTShape.h"
    COPYONLY
)

target_include_directories(Modeling PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# 依赖 Engine（Math、Mesh、AABB）
target_link_libraries(Modeling PUBLIC Engine)

# OCCT 头文件和库（仅 OCCT 后端编译期需要）
target_include_directories(Modeling PRIVATE
    ${OCCT_INCLUDE_DIR}
)

foreach(lib ${OCCT_LIBS})
    find_library(MODELING_OCCT_${lib} ${lib} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(MODELING_OCCT_${lib})
        target_link_libraries(Modeling PRIVATE ${MODELING_OCCT_${lib}})
    else()
        message(WARNING "OCCT library not found for Modeling: ${lib}")
    endif()
endforeach()

target_compile_definitions(Modeling PRIVATE
    BUILDING_MODELING
    $<$<CONFIG:Debug>:_DEBUG>
)

# MSVC: DLL 导出需要所有可见符号
if(MSVC)
    target_compile_options(Modeling PRIVATE /FS)
endif()
