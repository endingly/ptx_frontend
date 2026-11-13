file(GLOB_RECURSE cst_srcs CONFIGURE_DEPENDS src/*cpp)
add_library(ptx_frontend_cst ${cst_srcs})
set_property(TARGET ptx_frontend_cst PROPERTY EXPORT_NAME cst)
add_library(ptx_frontend::cst ALIAS ptx_frontend_cst)
register_headers(
    ptx_frontend_cst
    cst
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    ptx_frontend_cst
    PUBLIC
    ptx_frontend::lexer
    ptx_frontend::base
)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_cst_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_cst ${test_cst_srcs})
    target_sources(test_cst PRIVATE fuzz/lexer_cst_fuzz.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_cst PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::cst)
    # Each parser/fuzz regression runs in its own time-bounded CTest process.
    gtest_discover_tests(test_cst PROPERTIES TIMEOUT 10)
endif()
