find_package(GTest CONFIG REQUIRED)

add_executable(RHISubmissionTests
    resource_submission_tests.cpp
    bind_group_validation_tests.cpp
)
target_link_libraries(RHISubmissionTests PRIVATE rhi GTest::gtest GTest::gtest_main)
add_test(NAME RHISubmissionTests COMMAND RHISubmissionTests)
