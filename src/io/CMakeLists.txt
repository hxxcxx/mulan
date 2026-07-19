# IO - file import/export and document persistence helpers.
add_library(io STATIC
    assimp_importer.cpp
    obj_importer.cpp
    gltf_importer.cpp
    importer_registry.cpp
    importer_factory.cpp
    file_manager.cpp
    import_builder.cpp
    parsed_scene_loader.cpp
    detail/mesh_attribute_generator.cpp
)

mulan_copy_public_headers(io io
    file_importer.h
    file_manager.h
    gltf_importer.h
    import_builder.h
    import_result.h
    io_export.h
    obj_importer.h
    parsed_scene.h
    parsed_scene_loader.h
)

find_package(assimp CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(RapidObj CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)

target_link_libraries(io
    PUBLIC
        core
        document
        graphics
        scene
        asset
        modeling_core
    PRIVATE
        mulan::profiling
        assimp::assimp
        fastgltf::fastgltf
        rapidobj::rapidobj
        mikktspace::mikktspace
)

set_target_properties(io PROPERTIES OUTPUT_NAME mulan_io)

target_compile_definitions(io PRIVATE
    BUILDING_IO
    FASTGLTF_USE_NLOHMANN_JSON
    $<$<CONFIG:Debug>:_DEBUG>
)

target_precompile_headers(io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)
