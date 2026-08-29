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

    set(PTX_MODERN_OPERAND_TEST_SPEC_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    file(GLOB_RECURSE PTX_MODERN_OPERAND_TEST_SPEC_FILES CONFIGURE_DEPENDS
        "${PTX_MODERN_OPERAND_TEST_SPEC_DIR}/*.yaml")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${PTX_MODERN_OPERAND_TEST_SPEC_FILES})
    set(PTX_MODERN_OPERAND_TEST_GENERATED_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/test_generated")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC
        "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}/public")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE
        "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}/private")
    ptx_resolved_ir_list_generated_outputs(
        PTX_MODERN_OPERAND_TEST_GENERATED_FILES
        "${PTX_MODERN_OPERAND_TEST_SPEC_DIR}"
        "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}")
    set(PTX_MODERN_OPERAND_TEST_GENERATED_SRCS
        ${PTX_MODERN_OPERAND_TEST_GENERATED_FILES})
    list(FILTER PTX_MODERN_OPERAND_TEST_GENERATED_SRCS
        INCLUDE REGEX "\\.gen\\.cpp$")
    add_custom_command(
        OUTPUT ${PTX_MODERN_OPERAND_TEST_GENERATED_FILES}
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/python/scripts/gen_all.py"
                --spec-dir "${PTX_MODERN_OPERAND_TEST_SPEC_DIR}"
                --backend-spec "${PTX_RESOLVED_IR_BACKEND_SPEC}"
                --output "${PTX_MODERN_OPERAND_TEST_GENERATED_DIR}"
        DEPENDS ${PTX_MODERN_OPERAND_TEST_SPEC_FILES}
                ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
                ${PTX_RESOLVED_IR_BACKEND_SPEC}
                ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
                ${PTX_RESOLVED_IR_CODEGEN_FILES}
        COMMENT "Generating modern operand test fixture"
        VERBATIM)
    add_custom_target(modern_operand_resolved_ir_codegen
        DEPENDS ${PTX_MODERN_OPERAND_TEST_GENERATED_FILES})
    add_library(modern_operand_resolved_ir STATIC
        ${resolved_ir_srcs}
        ${PTX_MODERN_OPERAND_TEST_GENERATED_SRCS})
    add_dependencies(modern_operand_resolved_ir
        modern_operand_resolved_ir_codegen)
    target_include_directories(modern_operand_resolved_ir
        PUBLIC "${PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC}"
               "$<TARGET_PROPERTY:resolved_ir,INTERFACE_INCLUDE_DIRECTORIES>"
        PRIVATE "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}")
    target_link_libraries(modern_operand_resolved_ir
        PUBLIC ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
               ptx_frontend::common ptx_frontend::semantic fmt::fmt)
    add_executable(test_modern_operand_codegen
        test/test_modern_operand_codegen.cpp)
    target_link_libraries(test_modern_operand_codegen
        PRIVATE GTest::gtest GTest::gtest_main modern_operand_resolved_ir)
    gtest_discover_tests(test_modern_operand_codegen)

    add_test(
        NAME resolved_ir.modern_operand_codegen_self_heal
        COMMAND "${CMAKE_COMMAND}"
            "-DPTX_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DPTX_TEST_CONFIG=$<CONFIG>"
            "-DPTX_GENERATED_HEADER=${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}/resolved_value_domains.gen.hpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/codegen_self_heal.cmake"
    )
    set_tests_properties(resolved_ir.modern_operand_codegen_self_heal
        PROPERTIES RUN_SERIAL TRUE)

    add_test(
        NAME resolved_ir.modern_operand_codegen_topology_reconfigure
        COMMAND "${CMAKE_COMMAND}"
            "-DPTX_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DPTX_TEST_CONFIG=$<CONFIG>"
            "-DPTX_FIXTURE=${PTX_MODERN_OPERAND_TEST_SPEC_DIR}/modern_operand.yaml"
            "-DPTX_GENERATED_PRIVATE=${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/codegen_topology_reconfigure.cmake"
    )
    set_tests_properties(resolved_ir.modern_operand_codegen_topology_reconfigure
        PROPERTIES RUN_SERIAL TRUE)

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
