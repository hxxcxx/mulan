find_package(GTest CONFIG REQUIRED)

add_executable(ModelingTests
    shape_ops_registry_tests.cpp
    runtime_backend_tests.cpp
)
target_link_libraries(ModelingTests PRIVATE runtime modeling_core GTest::gtest GTest::gtest_main)
target_compile_definitions(ModelingTests PRIVATE
    MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND="${MULAN_EFFECTIVE_DEFAULT_SHAPE_OPS_BACKEND}"
)
add_dependencies(ModelingTests modeling_occt)
if(TARGET modeling_truck)
    add_dependencies(ModelingTests modeling_truck)
endif()

add_test(NAME ModelingTests COMMAND ModelingTests)

set(MODELING_TEST_RUNTIME_DIRS "${OCCT_BIN_DIR}")
if(OCCT_3RDPARTY_DIR)
    mulan_collect_runtime_bin_dirs(MODELING_TEST_OCCT_3RDPARTY_DIRS "${OCCT_3RDPARTY_DIR}")
    list(APPEND MODELING_TEST_RUNTIME_DIRS ${MODELING_TEST_OCCT_3RDPARTY_DIRS})
endif()

set(MODELING_TEST_ENVIRONMENT)
foreach(runtime_dir IN LISTS MODELING_TEST_RUNTIME_DIRS)
    list(APPEND MODELING_TEST_ENVIRONMENT "PATH=path_list_prepend:${runtime_dir}")
endforeach()
set_tests_properties(ModelingTests PROPERTIES
    ENVIRONMENT_MODIFICATION "${MODELING_TEST_ENVIRONMENT}"
)
