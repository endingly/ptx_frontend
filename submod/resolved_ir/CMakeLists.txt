file(GLOB_RECURSE resolved_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(resolved_ir ${resolved_ir_srcs} ${PTX_RESOLVED_IR_GENERATED_SRCS})
add_library(ptx_frontend::resolved_ir ALIAS resolved_ir)
register_headers(
    resolved_ir
    resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(
    resolved_ir
    PUBLIC $<BUILD_INTERFACE:${PTX_GEN_PUBLIC_DIR}>
    PRIVATE ${PTX_GEN_PRIVATE_DIR}
)
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    resolved_ir
    PUBLIC ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
            ptx_frontend::common fmt::fmt
    PRIVATE ptx_frontend::semantic
)
add_dependencies(resolved_ir auto_gen_code)

if(BUILD_TESTING)
    file(GLOB resolved_ir_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_resolved_ir ${resolved_ir_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_resolved_ir
        PRIVATE GTest::gtest GTest::gtest_main resolved_ir
    )
    gtest_discover_tests(test_resolved_ir)
endif()
