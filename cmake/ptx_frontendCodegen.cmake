include_guard(GLOBAL)
include(CMakeParseArguments)

function(ptx_frontend_generate)
    cmake_parse_arguments(
        PTX_CODEGEN
        ""
        "TARGET;SPEC_DIR;BACKEND_SPEC;OUTPUT_DIR"
        ""
        ${ARGN}
    )

    foreach(_required IN ITEMS TARGET SPEC_DIR BACKEND_SPEC OUTPUT_DIR)
        if(NOT PTX_CODEGEN_${_required})
            message(FATAL_ERROR
                "ptx_frontend_generate: ${_required} is required")
        endif()
    endforeach()

    if(NOT EXISTS "${PTX_CODEGEN_SPEC_DIR}")
        message(FATAL_ERROR
            "ptx_frontend_generate: SPEC_DIR does not exist: "
            "${PTX_CODEGEN_SPEC_DIR}")
    endif()
    if(NOT EXISTS "${PTX_CODEGEN_BACKEND_SPEC}")
        message(FATAL_ERROR
            "ptx_frontend_generate: BACKEND_SPEC does not exist: "
            "${PTX_CODEGEN_BACKEND_SPEC}")
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    if(TARGET "${PTX_CODEGEN_TARGET}")
        message(FATAL_ERROR
            "ptx_frontend_generate: TARGET already exists: "
            "${PTX_CODEGEN_TARGET}")
    endif()

    file(GLOB_RECURSE _spec_files CONFIGURE_DEPENDS
        "${PTX_CODEGEN_SPEC_DIR}/*.yaml")
    set(_instruction_schema
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/code_gen/resources/ptx-instr-v1.schema.yaml")
    set(_backend_schema
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/code_gen/resources/ptx-cpp-backend-v1.schema.yaml")
    file(GLOB_RECURSE _generator_files CONFIGURE_DEPENDS
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/base/*.py"
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/code_gen/*.py"
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/ir/*.py"
        "${ptx_frontend_CODEGEN_PYTHON_ROOT}/scripts/*.py")

    # Output topology is discovered at configure time, so any generator input
    # must cause CMake to rerun --list-outputs before the next build.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${_spec_files}
        "${_instruction_schema}"
        "${_backend_schema}"
        "${PTX_CODEGEN_BACKEND_SPEC}"
        ${_generator_files})

    execute_process(
        COMMAND
            "${Python3_EXECUTABLE}" "${ptx_frontend_CODEGEN_ENTRYPOINT}"
            --spec-dir "${PTX_CODEGEN_SPEC_DIR}"
            --backend-spec "${PTX_CODEGEN_BACKEND_SPEC}"
            --output "${PTX_CODEGEN_OUTPUT_DIR}"
            --list-outputs
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "ptx_frontend_generate: failed to list generated files: ${_error}")
    endif()

    string(REPLACE "\n" ";" _generated_files "${_output}")
    add_custom_command(
        OUTPUT ${_generated_files}
        COMMAND
            "${Python3_EXECUTABLE}" "${ptx_frontend_CODEGEN_ENTRYPOINT}"
            --spec-dir "${PTX_CODEGEN_SPEC_DIR}"
            --backend-spec "${PTX_CODEGEN_BACKEND_SPEC}"
            --output "${PTX_CODEGEN_OUTPUT_DIR}"
        DEPENDS
            ${_spec_files}
            "${_instruction_schema}"
            "${_backend_schema}"
            "${PTX_CODEGEN_BACKEND_SPEC}"
            ${_generator_files}
        COMMENT "Generating PTX frontend artifacts"
        VERBATIM
    )
    add_custom_target("${PTX_CODEGEN_TARGET}" DEPENDS ${_generated_files})

    set(ptx_frontend_GENERATED_FILES
        "${_generated_files}" PARENT_SCOPE)
endfunction()
