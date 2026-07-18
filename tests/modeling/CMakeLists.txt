find_package(GTest CONFIG REQUIRED)

add_executable(ModelingTests
    shape_ops_registry_tests.cpp
)
target_link_libraries(ModelingTests PRIVATE modeling_core GTest::gtest GTest::gtest_main)

add_test(NAME ModelingTests COMMAND ModelingTests)

# Linux 必须验证的不只是 OCCT 源码能链接，还包括 .so 部署、runtime
# 动态加载、跨 DSO 注册表以及一次真实的建模/离散调用。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET modeling_occt)
    add_executable(ModelingOcctPluginTests
        occt_plugin_integration_tests.cpp
    )
    target_link_libraries(ModelingOcctPluginTests PRIVATE
        runtime
        modeling_core
        GTest::gtest
        GTest::gtest_main
    )
    add_dependencies(ModelingOcctPluginTests modeling_occt)
    add_test(NAME ModelingOcctPluginTests COMMAND ModelingOcctPluginTests)
endif()
