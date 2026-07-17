find_package(GTest CONFIG REQUIRED)

add_executable(IOImportSecurityTests import_security_tests.cpp)
target_link_libraries(IOImportSecurityTests PRIVATE io GTest::gtest GTest::gtest_main)
add_test(NAME IOImportSecurityTests COMMAND IOImportSecurityTests)

add_executable(IOImportTests
    gltf_importer_tests.cpp
    mesh_attribute_generator_tests.cpp
    parsed_scene_loader_tests.cpp
)
target_include_directories(IOImportTests PRIVATE ${PROJECT_SOURCE_DIR}/src/io)
target_link_libraries(IOImportTests PRIVATE io GTest::gtest GTest::gtest_main)
add_test(NAME IOImportTests COMMAND IOImportTests)
