find_package(GTest CONFIG REQUIRED)

add_executable(ModelingTests
    shape_ops_registry_tests.cpp
)
target_link_libraries(ModelingTests PRIVATE modeling_core GTest::gtest GTest::gtest_main)

add_test(NAME ModelingTests COMMAND ModelingTests)
