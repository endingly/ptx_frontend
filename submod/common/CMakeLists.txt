# file(GLOB_RECURSE common_srcs src/*cpp)
add_library(ptx_frontend_common INTERFACE)
set_property(TARGET ptx_frontend_common PROPERTY EXPORT_NAME common)
# target_sources(ptx_frontend_common PRIVATE ${common_srcs})
add_library(ptx_frontend::common ALIAS ptx_frontend_common)
target_compile_features(ptx_frontend_common INTERFACE cxx_std_23)
register_headers(
    ptx_frontend_common
    common
    INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# if(BUILD_TESTING)
#     file(GLOB_RECURSE test_common_srcs test/*.cpp)
#     add_executable(test_common ${test_common_srcs})
#     find_package(GTest CONFIG REQUIRED)
#     target_link_libraries(test_common PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::common)
#     gtest_discover_tests(test_common)
# endif()
