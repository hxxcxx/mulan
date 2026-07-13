find_package(GTest CONFIG REQUIRED)

add_executable(CoreImageTests image_tests.cpp)
target_link_libraries(CoreImageTests PRIVATE core GTest::gtest GTest::gtest_main)
add_test(NAME CoreImageTests COMMAND CoreImageTests)

# CoreLogTests 自定义 main(注册全局日志环境)，故只链接 gtest。
add_executable(CoreLogTests log_tests.cpp)
target_link_libraries(CoreLogTests PRIVATE core GTest::gtest)
add_test(NAME CoreLogTests COMMAND CoreLogTests)
