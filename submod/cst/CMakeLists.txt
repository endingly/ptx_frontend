file(GLOB_RECURSE cst_srcs src/*cpp)
add_library(cst ${cst_srcs})
add_library(ptx_frontend::cst ALIAS cst)
register_headers(
    cst
    cst
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    cst
    PUBLIC ptx_frontend::lexer
)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_cst_srcs test/*.cpp)
    add_executable(test_cst ${test_cst_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_cst PRIVATE GTest::gtest GTest::gtest_main cst)
    gtest_discover_tests(test_cst)
endif()
