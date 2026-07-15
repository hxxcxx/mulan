find_package(GTest CONFIG REQUIRED)

add_executable(EditorActionTests
    editor_action_tests.cpp
)
target_link_libraries(EditorActionTests PRIVATE editor GTest::gtest GTest::gtest_main)
add_test(NAME EditorActionTests COMMAND EditorActionTests)
