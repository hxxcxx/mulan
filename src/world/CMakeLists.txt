# World - 领域实体层（静态库）
add_library(World STATIC
    Entity.cpp
    World.cpp
    Viewport.cpp
    SceneProxy.cpp
    StaticDrawList.cpp
    DynamicDrawList.cpp
    geometry/BoxGeometryData.cpp
    geometry/MeshGeometryData.cpp
    geometry/SolidGeometryData.cpp
    geometry/LineGeometryData.cpp
    geometry/ArcGeometryData.cpp
    geometry/PolylineGeometryData.cpp
    system/TransformSystem.cpp
    system/BoundsSystem.cpp
    system/RenderSystem.cpp
)

# 构建时将头文件复制到 build/include/mulan/world/...
file(GLOB_RECURSE WORLD_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
foreach(hdr ${WORLD_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${hdr}")
    configure_file("${hdr}" "${CMAKE_BINARY_DIR}/include/mulan/world/${rel}" COPYONLY)
endforeach()

target_include_directories(World PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

target_link_libraries(World PUBLIC Engine)

# OCCT（SolidGeometryData）
target_include_directories(World PRIVATE ${OCCT_INCLUDE_DIR})
foreach(lib ${OCCT_LIBS})
    find_library(OCCT_WORLD_${lib} ${lib} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(OCCT_WORLD_${lib})
        target_link_libraries(World PRIVATE ${OCCT_WORLD_${lib}})
    endif()
endforeach()

target_compile_definitions(World PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

# ── 内存测试 ──
add_executable(WorldTest WorldTest.cpp)
target_link_libraries(WorldTest PRIVATE World Engine)
target_include_directories(WorldTest PRIVATE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)
