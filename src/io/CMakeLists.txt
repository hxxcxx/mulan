# IO - file import/export and document persistence helpers.
add_library(io STATIC
    assimp_importer.cpp
    gltf_importer.cpp
    importer_registry.cpp
    importer_factory.cpp
    file_manager.cpp
    import_builder.cpp
    document.cpp
    document_editor.cpp
)

mulan_copy_public_headers(io io)

find_package(assimp CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

target_link_libraries(io
    PUBLIC
        core
        graphics
        scene
        asset
        modeling_core
    PRIVATE
        assimp::assimp
        fastgltf::fastgltf
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
