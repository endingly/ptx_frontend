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
           ptx_frontend::common ptx_frontend::semantic fmt::fmt
)
add_dependencies(resolved_ir resolved_ir_codegen)

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
        PRIVATE GTest::gtest GTest::gtest_main resolved_ir
    )
    gtest_discover_tests(test_resolved_ir)

    set(PTX_MODERN_OPERAND_TEST_SPEC
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/modern_operand.yaml")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/test_generated")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC
        "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}/public")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE
        "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}/private")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_SRCS
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}/resolved_ir_test.gen.cpp"
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}/syntax_descriptor.gen.cpp"
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}/resolved_descriptor.gen.cpp"
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}/resolved_ir_checker_descriptor.gen.cpp")
    add_custom_command(
        OUTPUT "${PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC}/resolved_ir.gen.hpp"
               ${PTX_MODERN_OPERAND_TEST_GENERATED_SRCS}
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/python/scripts/gen_all.py"
                --spec-dir "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures"
                --backend-spec "${PTX_RESOLVED_IR_BACKEND_SPEC}"
                --output "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}"
        DEPENDS ${PTX_MODERN_OPERAND_TEST_SPEC}
                ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
                ${PTX_RESOLVED_IR_BACKEND_SPEC}
                ${PTX_RESOLVED_IR_CODEGEN_FILES}
        COMMENT "Generating modern operand test fixture"
        VERBATIM)
    add_executable(test_modern_operand_codegen
        test/test_modern_operand_codegen.cpp
        ${PTX_MODERN_OPERAND_TEST_GENERATED_SRCS})
    target_include_directories(test_modern_operand_codegen PRIVATE
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC}"
        "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}")
    target_link_libraries(test_modern_operand_codegen
        PRIVATE GTest::gtest GTest::gtest_main resolved_ir)
    gtest_discover_tests(test_modern_operand_codegen)

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
