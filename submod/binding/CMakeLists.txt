file(GLOB_RECURSE binding_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptx_frontend_binding STATIC ${binding_srcs})
set_property(TARGET ptx_frontend_binding PROPERTY EXPORT_NAME binding)
add_library(ptx_frontend::binding ALIAS ptx_frontend_binding)
register_headers(ptx_frontend_binding binding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    ptx_frontend_binding
    PUBLIC
    ptx_frontend::base
    ptx_frontend::syntax
    fmt::fmt
)

if(BUILD_TESTING)
    file(GLOB binding_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_binding ${binding_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_binding PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::binding)
    gtest_discover_tests(test_binding)
endif()
