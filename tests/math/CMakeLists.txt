add_executable(MathTests
    math_tests.cpp
)

target_link_libraries(MathTests PRIVATE math)

add_test(NAME MathTests COMMAND MathTests)
