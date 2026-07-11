find_package(GTest CONFIG REQUIRED)

add_executable(CoreImageTests image_tests.cpp)
target_link_libraries(CoreImageTests PRIVATE core GTest::gtest GTest::gtest_main)
add_test(NAME CoreImageTests COMMAND CoreImageTests)
