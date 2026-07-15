find_package(GTest CONFIG REQUIRED)

add_executable(InteractionTests
    interaction_tests.cpp
)
target_link_libraries(InteractionTests PRIVATE interaction render GTest::gtest GTest::gtest_main)
add_test(NAME InteractionTests COMMAND InteractionTests)
