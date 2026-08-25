include(generate_ptx_frontend)

file(GLOB_RECURSE resolved_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(resolved_ir STATIC
    ${resolved_ir_srcs}
    ${PTX_RESOLVED_IR_GENERATED_SRCS}
)
add_library(ptx_frontend::resolved_ir ALIAS resolved_ir)
register_headers(
    resolved_ir
    resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(
    resolved_ir
    PUBLIC $<BUILD_INTERFACE:${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}>
    PRIVATE ${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}
)
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    resolved_ir
    PUBLIC ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
           ptx_frontend::common fmt::fmt
    PRIVATE ptx_frontend::semantic
)
add_dependencies(resolved_ir resolved_ir_codegen)

install(
    DIRECTORY "${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

if(BUILD_TESTING)
    file(GLOB resolved_ir_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_resolved_ir ${resolved_ir_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_resolved_ir
        PRIVATE GTest::gtest GTest::gtest_main resolved_ir
    )
    gtest_discover_tests(test_resolved_ir)

    add_test(
        NAME ptx_frontend.package_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DPTX_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPTX_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DPTX_TEST_CONFIG=$<CONFIG>"
            "-DPTX_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DPTX_FMT_DIR=${fmt_DIR}"
            "-DPTX_MAGIC_ENUM_DIR=${magic_enum_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/package_consumer/run_test.cmake"
    )
endif()
