find_package(GTest CONFIG REQUIRED)

add_executable(IOImportSecurityTests import_security_tests.cpp)
target_link_libraries(IOImportSecurityTests PRIVATE io GTest::gtest GTest::gtest_main)
add_test(NAME IOImportSecurityTests COMMAND IOImportSecurityTests)
