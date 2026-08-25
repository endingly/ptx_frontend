file(GLOB_RECURSE semantic_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(semantic STATIC ${semantic_srcs})
add_library(ptx_frontend::semantic ALIAS semantic)
register_headers(semantic semantic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    semantic
    PUBLIC
    ptx_frontend::binding
    fmt::fmt
)

if(BUILD_TESTING)
    file(GLOB semantic_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_semantic ${semantic_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_semantic PRIVATE GTest::gtest GTest::gtest_main semantic)
    gtest_discover_tests(test_semantic)
endif()
