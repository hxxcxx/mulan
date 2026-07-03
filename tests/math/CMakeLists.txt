add_executable(MathTests
    math_tests.cpp
)

target_link_libraries(MathTests PRIVATE Math)

add_test(NAME MathTests COMMAND MathTests)
