include(generate_ptx_frontend)

file(GLOB_RECURSE resolved_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptx_frontend_resolved_ir STATIC
    ${resolved_ir_srcs}
    ${PTX_RESOLVED_IR_GENERATED_SRCS}
)
set_property(TARGET ptx_frontend_resolved_ir PROPERTY EXPORT_NAME resolved_ir)
add_library(ptx_frontend::resolved_ir ALIAS ptx_frontend_resolved_ir)
register_headers(
    ptx_frontend_resolved_ir
    resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(
    ptx_frontend_resolved_ir
    PUBLIC $<BUILD_INTERFACE:${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}>
    PRIVATE ${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}
)
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    ptx_frontend_resolved_ir
    PUBLIC ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
           ptx_frontend::common ptx_frontend::semantic fmt::fmt
)
add_dependencies(ptx_frontend_resolved_ir resolved_ir_codegen)

if(PTX_FRONTEND_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

install(
    DIRECTORY "${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

if(BUILD_TESTING)
    file(GLOB resolved_ir_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    list(REMOVE_ITEM resolved_ir_test_srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_modern_operand_codegen.cpp")
    add_executable(test_resolved_ir ${resolved_ir_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_resolved_ir
        PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::resolved_ir
    )
    gtest_discover_tests(test_resolved_ir)
endif()
