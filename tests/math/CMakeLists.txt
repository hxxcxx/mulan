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

add_executable(ConvexHullTests
    convex_hull_tests.cpp
)
target_link_libraries(ConvexHullTests PRIVATE math)
add_test(NAME ConvexHullTests COMMAND ConvexHullTests)

add_executable(IntersectTests
    intersect_tests.cpp
)
target_link_libraries(IntersectTests PRIVATE math)
add_test(NAME IntersectTests COMMAND IntersectTests)

add_executable(TriangulationTests
    triangulation_tests.cpp
)
target_link_libraries(TriangulationTests PRIVATE math)
add_test(NAME TriangulationTests COMMAND TriangulationTests)

add_executable(VoronoiDcelTests
    voronoi_dcel_tests.cpp
)
target_link_libraries(VoronoiDcelTests PRIVATE math)
add_test(NAME VoronoiDcelTests COMMAND VoronoiDcelTests)

add_executable(SpatialTests
    spatial_tests.cpp
)
target_link_libraries(SpatialTests PRIVATE math)
add_test(NAME SpatialTests COMMAND SpatialTests)
