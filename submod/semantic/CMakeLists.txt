file(GLOB_RECURSE semantic_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptx_frontend_semantic STATIC ${semantic_srcs})
set_property(TARGET ptx_frontend_semantic PROPERTY EXPORT_NAME semantic)
add_library(ptx_frontend::semantic ALIAS ptx_frontend_semantic)
register_headers(ptx_frontend_semantic semantic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    ptx_frontend_semantic
    PUBLIC
    ptx_frontend::binding
    fmt::fmt
)

if(BUILD_TESTING)
    file(GLOB semantic_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_semantic ${semantic_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_semantic PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::semantic)
    gtest_discover_tests(test_semantic)
endif()
