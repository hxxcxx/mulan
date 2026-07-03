add_executable(GeoTests
    geo_tests.cpp
)

target_link_libraries(GeoTests PRIVATE Geo)

add_test(NAME GeoTests COMMAND GeoTests)
