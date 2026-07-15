find_package(GTest CONFIG REQUIRED)

add_executable(EditorActionTests
    editor_action_tests.cpp
)
target_include_directories(EditorActionTests PRIVATE ${PROJECT_SOURCE_DIR}/src/editor)
target_link_libraries(EditorActionTests PRIVATE editor view GTest::gtest GTest::gtest_main)
add_test(NAME EditorActionTests COMMAND EditorActionTests)

add_executable(DocumentIntegrityTests
    document_integrity_tests.cpp
)
target_include_directories(DocumentIntegrityTests PRIVATE ${PROJECT_SOURCE_DIR}/src/editor)
target_link_libraries(DocumentIntegrityTests PRIVATE editor view GTest::gtest GTest::gtest_main)
add_test(NAME DocumentIntegrityTests COMMAND DocumentIntegrityTests)

# 公开头必须仅依赖 editor 的 PUBLIC 接口，不能借用测试或 app 的私有包含路径。
add_executable(EditorPublicHeaderCompileTest
    document_view_public_header_compile_test.cpp
)
target_link_libraries(EditorPublicHeaderCompileTest PRIVATE editor)
add_test(NAME EditorPublicHeaderCompileTest COMMAND EditorPublicHeaderCompileTest)
