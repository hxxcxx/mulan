find_package(GTest CONFIG REQUIRED)

add_executable(RHISubmissionTests
    resource_submission_tests.cpp
    bind_group_validation_tests.cpp
    transient_uniform_allocator_tests.cpp
)
target_link_libraries(RHISubmissionTests PRIVATE rhi GTest::gtest GTest::gtest_main)

if(TARGET rhi_dx11)
    target_sources(RHISubmissionTests PRIVATE dx11_transient_uniform_tests.cpp)
    target_include_directories(RHISubmissionTests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/rhi_dx11)
    target_link_libraries(RHISubmissionTests PRIVATE rhi_dx11 d3d11)
endif()

add_test(NAME RHISubmissionTests COMMAND RHISubmissionTests)
