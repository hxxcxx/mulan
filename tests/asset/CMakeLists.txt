find_package(GTest CONFIG REQUIRED)

add_executable(AssetRevisionTests
    asset_revision_tests.cpp
)
target_link_libraries(AssetRevisionTests PRIVATE asset GTest::gtest GTest::gtest_main)
add_test(NAME AssetRevisionTests COMMAND AssetRevisionTests)
