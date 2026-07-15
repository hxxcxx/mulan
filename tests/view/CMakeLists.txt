find_package(GTest CONFIG REQUIRED)

add_executable(ViewRuntimeTests
    render_submission_builder_tests.cpp
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
