find_package(GTest CONFIG REQUIRED)

add_executable(SceneRevisionTests
    scene_revision_tests.cpp
)
target_link_libraries(SceneRevisionTests PRIVATE scene GTest::gtest GTest::gtest_main)
add_test(NAME SceneRevisionTests COMMAND SceneRevisionTests)
