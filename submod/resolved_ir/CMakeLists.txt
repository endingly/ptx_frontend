# Active direct-class resolved IR; generation shares the canonical lowering plan.
find_package(fmt CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PTX_FRONTEND_CODEGEN_JOBS "6" CACHE STRING
    "Maximum concurrent Resolved IR generator artifact writers")
if(NOT PTX_FRONTEND_CODEGEN_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "PTX_FRONTEND_CODEGEN_JOBS must be a positive integer")
endif()

set(resolved_spec_dir "${PROJECT_SOURCE_DIR}/instructions/ptx_spec")
set(resolved_backend
    "${PROJECT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")
set(resolved_backend_schema
    "${PROJECT_SOURCE_DIR}/instructions/ptx-cpp-backend-v2.schema.yaml")
set(resolved_instruction_schema
    "${PROJECT_SOURCE_DIR}/instructions/ptx-instr-v1.schema.yaml")
set(resolved_generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(resolved_generated_private "${resolved_generated_root}/private")
set(resolved_generated_public "${resolved_generated_root}/public")
file(GLOB resolved_spec_files CONFIGURE_DEPENDS
    "${resolved_spec_dir}/*.yaml")
file(GLOB_RECURSE resolved_codegen_python CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/base/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/code_gen/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${resolved_spec_files}
    "${resolved_instruction_schema}"
    "${resolved_backend}"
    "${resolved_backend_schema}"
    ${resolved_codegen_python})
set(resolved_codegen_command
    "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PROJECT_SOURCE_DIR}/python/src"
    "${Python3_EXECUTABLE}" -m ptx_frontend.code_gen
    --spec-dir "${resolved_spec_dir}"
    --backend-spec "${resolved_backend}"
    --output "${resolved_generated_root}"
    --jobs "${PTX_FRONTEND_CODEGEN_JOBS}")
execute_process(
    COMMAND ${resolved_codegen_command} --list-outputs
    OUTPUT_VARIABLE resolved_generated_list
    ERROR_VARIABLE resolved_codegen_error
    RESULT_VARIABLE resolved_codegen_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT resolved_codegen_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to discover resolved IR outputs: ${resolved_codegen_error}")
endif()
string(REPLACE "\n" ";" resolved_generated_outputs
    "${resolved_generated_list}")
list(FILTER resolved_generated_outputs EXCLUDE REGEX "^$")
list(FILTER resolved_generated_outputs INCLUDE REGEX "\\.gen\\.(hpp|cpp)$")
list(FILTER resolved_generated_outputs EXCLUDE REGEX "^$")
set(resolved_generated_sources ${resolved_generated_outputs})
list(FILTER resolved_generated_sources INCLUDE REGEX "\\.gen\\.cpp$")
add_custom_command(
    OUTPUT ${resolved_generated_outputs}
    COMMAND ${resolved_codegen_command}
    DEPENDS ${resolved_spec_files} "${resolved_instruction_schema}"
        "${resolved_backend}" "${resolved_backend_schema}"
        ${resolved_codegen_python}
    COMMENT "Generating direct resolved IR forms and descriptors"
    VERBATIM)
add_custom_target(resolved_ir_codegen
    DEPENDS ${resolved_generated_outputs})

add_library(ptx_frontend_resolved_ir STATIC
    ${resolved_generated_sources}
    src/ptx_resolved_ir.cpp
    src/ptx_resolved_ir_modifiers.cpp
    src/ptx_resolved_ir_literals.cpp
    src/ptx_resolved_ir_bound_operands.cpp
    src/ptx_resolved_ir_layout.cpp
    src/ptx_resolved_ir_checker.cpp
    src/ptx_storage_declarations.cpp
    src/ptx_resolved_module.cpp
    src/ptx_module_availability.cpp
    src/ptx_resolved_function.cpp)
set_property(TARGET ptx_frontend_resolved_ir
    PROPERTY EXPORT_NAME resolved_ir)
add_library(ptx_frontend::resolved_ir
    ALIAS ptx_frontend_resolved_ir)
register_headers(ptx_frontend_resolved_ir resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ptx_frontend_resolved_ir
    PUBLIC $<BUILD_INTERFACE:${resolved_generated_public}>
    PRIVATE "${resolved_generated_private}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ptx_frontend_resolved_ir PUBLIC
    ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
    ptx_frontend::common ptx_frontend::semantic fmt::fmt)
add_dependencies(ptx_frontend_resolved_ir
    resolved_ir_codegen)
if(PTX_FRONTEND_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
if(BUILD_TESTING)
    add_executable(resolved_ir_smoke test/smoke.cpp)
    target_link_libraries(resolved_ir_smoke PRIVATE
        ptx_frontend::resolved_ir)
    add_test(NAME resolved_ir_smoke
        COMMAND resolved_ir_smoke)

    find_package(GTest CONFIG REQUIRED)
    file(GLOB resolved_test_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
    list(REMOVE_ITEM resolved_test_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.cpp")
    add_executable(test_resolved_ir ${resolved_test_sources})
    target_link_libraries(test_resolved_ir PRIVATE
        GTest::gtest GTest::gtest_main ptx_frontend::resolved_ir)
    target_compile_options(test_resolved_ir PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-gline-tables-only>)
    gtest_discover_tests(test_resolved_ir)
endif()
