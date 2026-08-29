if(NOT DEFINED PTX_BINARY_DIR OR NOT DEFINED PTX_GENERATED_HEADER)
    message(FATAL_ERROR "PTX_BINARY_DIR and PTX_GENERATED_HEADER are required")
endif()

if(NOT EXISTS "${PTX_GENERATED_HEADER}")
    message(FATAL_ERROR
        "Expected generated header before self-heal test: ${PTX_GENERATED_HEADER}")
endif()

file(REMOVE "${PTX_GENERATED_HEADER}")
if(EXISTS "${PTX_GENERATED_HEADER}")
    message(FATAL_ERROR "Failed to remove generated header: ${PTX_GENERATED_HEADER}")
endif()

set(_build_command
    "${CMAKE_COMMAND}" --build "${PTX_BINARY_DIR}"
    --target modern_operand_resolved_ir)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${PTX_TEST_CONFIG}")
endif()

execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_stdout
    ERROR_VARIABLE _build_stderr)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR
        "Self-heal build failed (${_build_result}):\n"
        "${_build_stdout}${_build_stderr}")
endif()

if(NOT EXISTS "${PTX_GENERATED_HEADER}")
    message(FATAL_ERROR
        "Self-heal build did not restore ${PTX_GENERATED_HEADER}:\n"
        "${_build_stdout}${_build_stderr}")
endif()
