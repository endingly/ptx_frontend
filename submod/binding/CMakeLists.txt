file(GLOB_RECURSE binding_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(binding ${binding_srcs})
add_library(ptx_frontend::binding ALIAS binding)
register_headers(binding binding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    binding
    PUBLIC ptx_frontend::base ptx_frontend::syntax
    PRIVATE fmt::fmt
)

if(BUILD_TESTING)
    file(GLOB binding_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_binding ${binding_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_binding PRIVATE GTest::gtest GTest::gtest_main binding)
    gtest_discover_tests(test_binding)
endif()
