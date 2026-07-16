find_package(GTest CONFIG REQUIRED)

add_executable(ViewRuntimeTests
    asset_gpu_registry_tests.cpp
    geometry_draw_batch_tests.cpp
    material_cache_tests.cpp
    operator_routing_tests.cpp
    primitive_pick_index_tests.cpp
    render_compiler_tests.cpp
    render_scene_spatial_index_tests.cpp
    render_submission_builder_tests.cpp
    render_world_sync_incremental_tests.cpp
    render_worker_protocol_tests.cpp
)

target_include_directories(ViewRuntimeTests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/view
)

target_link_libraries(ViewRuntimeTests PRIVATE
    view
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME ViewRuntimeTests COMMAND ViewRuntimeTests)
