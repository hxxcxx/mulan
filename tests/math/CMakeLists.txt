find_package(GTest CONFIG REQUIRED)

add_executable(MathTests
    math_tests.cpp
    curve_surface_tests.cpp
    convex_hull_tests.cpp
    intersect_tests.cpp
    triangulation_tests.cpp
    voronoi_dcel_tests.cpp
    spatial_tests.cpp
)
target_link_libraries(MathTests PRIVATE math GTest::gtest GTest::gtest_main)
add_test(NAME MathTests COMMAND MathTests)
