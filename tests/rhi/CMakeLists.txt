find_package(GTest CONFIG REQUIRED)

add_executable(RHISubmissionTests
    backend_contract_tests.cpp
    backend_module_tests.cpp
    bind_group_validation_tests.cpp
    device_lifetime_tests.cpp
    transient_uniform_allocator_tests.cpp
)
target_link_libraries(RHISubmissionTests PRIVATE rhi GTest::gtest GTest::gtest_main)
target_compile_definitions(RHISubmissionTests PRIVATE
    MULAN_TEST_HAS_RHI_VULKAN=$<BOOL:$<TARGET_EXISTS:rhi_vulkan>>
    MULAN_TEST_HAS_RHI_D3D12=$<BOOL:$<TARGET_EXISTS:rhi_dx12>>
    MULAN_TEST_HAS_RHI_D3D11=$<BOOL:$<TARGET_EXISTS:rhi_dx11>>
    # Linux CI 没有稳定的原生窗口服务器；Linux OpenGL 本轮覆盖编译和链接，
    # 带窗口的后端契约保留给 Windows，运行验证后续进入专用图形环境。
    MULAN_TEST_HAS_RHI_OPENGL=$<AND:$<BOOL:$<TARGET_EXISTS:rhi_opengl>>,$<BOOL:${WIN32}>>
)

if(TARGET rhi_vulkan)
    target_link_libraries(RHISubmissionTests PRIVATE rhi_vulkan)
endif()

if(TARGET rhi_opengl)
    target_link_libraries(RHISubmissionTests PRIVATE rhi_opengl)
endif()

if(TARGET rhi_dx11)
    target_sources(RHISubmissionTests PRIVATE dx11_transient_uniform_tests.cpp)
    target_include_directories(RHISubmissionTests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/rhi_dx11)
    target_link_libraries(RHISubmissionTests PRIVATE rhi_dx11 d3d11)
endif()

if(TARGET rhi_dx12)
    target_sources(RHISubmissionTests PRIVATE dx12_transient_uniform_tests.cpp)
    target_include_directories(RHISubmissionTests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/rhi_dx12)
    target_link_libraries(RHISubmissionTests PRIVATE rhi_dx12 d3d12 dxgi)
endif()

add_test(NAME RHISubmissionTests COMMAND RHISubmissionTests)
