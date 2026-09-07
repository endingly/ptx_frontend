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

    if(NOT DEFINED PTX_MODERN_OPERAND_TEST_SPEC_DIR)
        set(PTX_MODERN_OPERAND_TEST_SPEC_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    endif()
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
    # Build these objects once for both the fixture executable and the topology
    # regression, which directly compiles the same generated source set.
    add_library(modern_operand_resolved_ir_generated OBJECT
        ${PTX_MODERN_OPERAND_TEST_GENERATED_SRCS})
    add_dependencies(modern_operand_resolved_ir_generated
        modern_operand_resolved_ir_codegen)
    target_include_directories(modern_operand_resolved_ir_generated
        PUBLIC "${PTX_MODERN_OPERAND_TEST_GENERATED_PUBLIC}"
               "$<TARGET_PROPERTY:resolved_ir,INTERFACE_INCLUDE_DIRECTORIES>"
        PRIVATE "${PTX_MODERN_OPERAND_TEST_GENERATED_PRIVATE}")
    target_link_libraries(modern_operand_resolved_ir_generated
        PRIVATE ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
                ptx_frontend::common ptx_frontend::semantic fmt::fmt)
    add_library(modern_operand_resolved_ir STATIC
        ${resolved_ir_srcs}
        $<TARGET_OBJECTS:modern_operand_resolved_ir_generated>)
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
            "-DPTX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPTX_TEST_CONFIG=$<CONFIG>"
            "-DPTX_FIXTURE=${PTX_MODERN_OPERAND_TEST_SPEC_DIR}/modern_operand.yaml"
            "-DPTX_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/topology_reconfigure"
            "-DPTX_INITIAL_CACHE=${CMAKE_CURRENT_BINARY_DIR}/topology_reconfigure_initial_cache.cmake"
            "-DPTX_DISABLE_NESTED_TOOLCHAIN=ON"
            "-DPTX_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DPTX_CMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DPTX_CMAKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DPTX_CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DPTX_CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DPTX_CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DPTX_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DPTX_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/codegen_topology_reconfigure.cmake"
    )
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/topology_reconfigure_initial_cache.cmake"
        CONTENT
"set(Python3_EXECUTABLE [==[${Python3_EXECUTABLE}]==] CACHE FILEPATH \"\" FORCE)
set(fmt_DIR [==[${fmt_DIR}]==] CACHE PATH \"\" FORCE)
set(magic_enum_DIR [==[${magic_enum_DIR}]==] CACHE PATH \"\" FORCE)
set(GTest_DIR [==[${GTest_DIR}]==] CACHE PATH \"\" FORCE)
set(CMAKE_PREFIX_PATH [==[${CMAKE_PREFIX_PATH}]==] CACHE STRING \"\" FORCE)
")
    add_test(
        NAME ptx_frontend.package_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DPTX_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPTX_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DPTX_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DPTX_TEST_CONFIG=$<CONFIG>"
            "-DPTX_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DPTX_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DPTX_CMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DPTX_CMAKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DPTX_CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DPTX_CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DPTX_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DPTX_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPTX_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}"
            "-DPTX_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
            "-DPTX_FMT_DIR=${fmt_DIR}"
            "-DPTX_MAGIC_ENUM_DIR=${magic_enum_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/package_consumer/run_test.cmake"
    )
endif()
