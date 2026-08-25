# file(GLOB_RECURSE common_srcs src/*cpp)
add_library(common INTERFACE)
# target_sources(common PRIVATE ${common_srcs})
add_library(ptx_frontend::common ALIAS common)
target_compile_features(common INTERFACE cxx_std_23)
register_headers(
    common
    common
    INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# if(BUILD_TESTING)
#     file(GLOB_RECURSE test_common_srcs test/*.cpp)
#     add_executable(test_common ${test_common_srcs})
#     find_package(GTest CONFIG REQUIRED)
#     target_link_libraries(test_common PRIVATE GTest::gtest GTest::gtest_main common)
#     gtest_discover_tests(test_common)
# endif()
