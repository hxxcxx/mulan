add_executable(MathTests
    math_tests.cpp
)
target_link_libraries(MathTests PRIVATE math)
add_test(NAME MathTests COMMAND MathTests)

add_executable(CurveSurfaceTests
    curve_surface_tests.cpp
)
target_link_libraries(CurveSurfaceTests PRIVATE math)
add_test(NAME CurveSurfaceTests COMMAND CurveSurfaceTests)
