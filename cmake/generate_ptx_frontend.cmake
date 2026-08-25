include_guard(DIRECTORY)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PTX_RESOLVED_IR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/public")
set(PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/private")

file(GLOB_RECURSE PTX_RESOLVED_IR_SPEC_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/instructions/ptx_spec/*.yaml")
set(PTX_RESOLVED_IR_INSTRUCTION_SCHEMA
    "${PROJECT_SOURCE_DIR}/instructions/schemas/ptx-instr-v1.schema.yaml")
set(PTX_RESOLVED_IR_BACKEND_SPEC
    "${PROJECT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")
set(PTX_RESOLVED_IR_BACKEND_SCHEMA
    "${PROJECT_SOURCE_DIR}/instructions/schemas/ptx-cpp-backend-v1.schema.yaml")
file(GLOB_RECURSE PTX_RESOLVED_IR_CODEGEN_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/base/*.py"
    "${PROJECT_SOURCE_DIR}/python/code_gen/*.py"
    "${PROJECT_SOURCE_DIR}/python/ir/*.py"
    "${PROJECT_SOURCE_DIR}/python/scripts/*.py")

# A spec or generator change can alter the declared source topology, so rerun
# --list-outputs before CMake attaches generated sources to resolved_ir.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PTX_RESOLVED_IR_SPEC_FILES}
    ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
    ${PTX_RESOLVED_IR_BACKEND_SPEC}
    ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
    ${PTX_RESOLVED_IR_CODEGEN_FILES})

execute_process(
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/python/scripts/gen_all.py"
            --spec-dir "${PROJECT_SOURCE_DIR}/instructions/ptx_spec"
            --backend-spec "${PTX_RESOLVED_IR_BACKEND_SPEC}"
            --output "${PTX_RESOLVED_IR_GENERATED_DIR}" --list-outputs
    RESULT_VARIABLE PTX_RESOLVED_IR_GENERATED_FILES_RESULT
    OUTPUT_VARIABLE PTX_RESOLVED_IR_GENERATED_FILES_OUTPUT
    ERROR_VARIABLE PTX_RESOLVED_IR_GENERATED_FILES_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT PTX_RESOLVED_IR_GENERATED_FILES_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to determine generated files: ${PTX_RESOLVED_IR_GENERATED_FILES_ERROR}")
endif()

string(REPLACE "\n" ";" PTX_RESOLVED_IR_GENERATED_FILES
    "${PTX_RESOLVED_IR_GENERATED_FILES_OUTPUT}")
add_custom_command(
    OUTPUT ${PTX_RESOLVED_IR_GENERATED_FILES}
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/python/scripts/gen_all.py"
            --spec-dir "${PROJECT_SOURCE_DIR}/instructions/ptx_spec"
            --backend-spec "${PTX_RESOLVED_IR_BACKEND_SPEC}"
            --output "${PTX_RESOLVED_IR_GENERATED_DIR}"
    DEPENDS
        ${PTX_RESOLVED_IR_SPEC_FILES}
        ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
        ${PTX_RESOLVED_IR_BACKEND_SPEC}
        ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
        ${PTX_RESOLVED_IR_CODEGEN_FILES}
    COMMENT "Generating resolved-IR sources"
    VERBATIM)
add_custom_target(resolved_ir_codegen DEPENDS ${PTX_RESOLVED_IR_GENERATED_FILES})

set(PTX_RESOLVED_IR_GENERATED_SRCS ${PTX_RESOLVED_IR_GENERATED_FILES})
list(FILTER PTX_RESOLVED_IR_GENERATED_SRCS INCLUDE REGEX "\\.gen\\.cpp$")
