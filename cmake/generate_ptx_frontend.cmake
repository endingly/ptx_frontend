include_guard(DIRECTORY)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# -----------------------------------------------------------------------------
# Generated output locations
# -----------------------------------------------------------------------------
set(
    PTX_RESOLVED_IR_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set(
    PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/public"
)

set(
    PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/private"
)

# -----------------------------------------------------------------------------
# Generator inputs
# -----------------------------------------------------------------------------
set(
    PTX_RESOLVED_IR_SPEC_DIR
    "${PROJECT_SOURCE_DIR}/instructions/ptx_spec"
)

file(
    GLOB_RECURSE
    PTX_RESOLVED_IR_SPEC_FILES
    CONFIGURE_DEPENDS
    "${PTX_RESOLVED_IR_SPEC_DIR}/*.yaml"
)

set(
    PTX_RESOLVED_IR_INSTRUCTION_SCHEMA
    "${PROJECT_SOURCE_DIR}/instructions/ptx-instr-v1.schema.yaml"
)

set(
    PTX_RESOLVED_IR_BACKEND_SPEC
    "${PROJECT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
)

set(
    PTX_RESOLVED_IR_BACKEND_SCHEMA
    "${PROJECT_SOURCE_DIR}/instructions/ptx-cpp-backend-v2.schema.yaml"
)

file(
    GLOB_RECURSE
    PTX_RESOLVED_IR_CODEGEN_FILES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/base/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/code_gen/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/scripts/*.py"
)

set(
    PTX_RESOLVED_IR_GENERATOR
    "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/scripts/gen_all.py"
)

# A specification or generator change may alter:
#
# * the set of codegen categories;
# * the specification files owned by each category;
# * the generated output topology.
#
# Therefore CMake must rerun --describe-build whenever one of these inputs
# changes, before attaching generated sources to resolved_ir.
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS

    ${PTX_RESOLVED_IR_SPEC_FILES}
    ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
    ${PTX_RESOLVED_IR_BACKEND_SPEC}
    ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
    ${PTX_RESOLVED_IR_CODEGEN_FILES}
)

# -----------------------------------------------------------------------------
# JSON helpers
# -----------------------------------------------------------------------------

# Convert a JSON array at the supplied object path into a normal CMake list.
#
# Example:
#
# ptx_json_array_to_list(
# outputs
# "${description}"
# categories 0 outputs
# )
#
# The result is returned through output_variable.
function(
    ptx_json_array_to_list
    output_variable
    json
)
    set(_json_path ${ARGN})

    string(
        JSON _length
        LENGTH
        "${json}"
        ${_json_path}
    )

    set(_items)

    if(_length GREATER 0)
        math(
            EXPR _last_index
            "${_length} - 1"
        )

        foreach(_index RANGE 0 ${_last_index})
            string(
                JSON _item
                GET
                "${json}"
                ${_json_path}
                ${_index}
            )

            list(
                APPEND _items
                "${_item}"
            )
        endforeach()
    endif()

    set(
        ${output_variable}
        "${_items}"
        PARENT_SCOPE
    )
endfunction()

# -----------------------------------------------------------------------------
# Configure-time build graph discovery
# -----------------------------------------------------------------------------

# Ask the Python generator to describe the generated build graph without
# creating or modifying generated files.
#
# Expected JSON shape:
#
# {
# "categories": [
# {
# "name": "...",
# "spec_files": ["..."],
# "outputs": ["..."]
# }
# ],
# "global_outputs": ["..."],
# "all_outputs": ["..."]
# }
function(
    ptx_resolved_ir_describe_generated_build
    output_variable
    spec_dir
    output_dir
)
    execute_process(
        COMMAND
        ${Python3_EXECUTABLE}
        "${PTX_RESOLVED_IR_GENERATOR}"

        --spec-dir
        "${spec_dir}"

        --backend-spec
        "${PTX_RESOLVED_IR_BACKEND_SPEC}"

        --output
        "${output_dir}"

        --describe-build

        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _result EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to describe generated Resolved IR build:\n${_error}"
        )
    endif()

    if(_output STREQUAL "")
        message(
            FATAL_ERROR
            "Resolved IR generator returned an empty build description"
        )
    endif()

    set(
        ${output_variable}
        "${_output}"
        PARENT_SCOPE
    )
endfunction()

ptx_resolved_ir_describe_generated_build(
    PTX_RESOLVED_IR_BUILD_DESCRIPTION
    "${PTX_RESOLVED_IR_SPEC_DIR}"
    "${PTX_RESOLVED_IR_GENERATED_DIR}"
)

# -----------------------------------------------------------------------------
# Complete output inventory
# -----------------------------------------------------------------------------

# Preserve the generator's canonical output ordering rather than reconstructing
# the complete output list from category groups inside CMake.
ptx_json_array_to_list(
    PTX_RESOLVED_IR_GENERATED_FILES
    "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
    all_outputs
)

if(NOT PTX_RESOLVED_IR_GENERATED_FILES)
    message(
        FATAL_ERROR
        "Resolved IR generator described no generated outputs"
    )
endif()

# -----------------------------------------------------------------------------
# Global generated artifacts
# -----------------------------------------------------------------------------

# Global artifacts depend on the complete normalized instruction model.
#
# Examples include:
#
# * the complete ResolvedInstruction union;
# * aggregate compatibility headers;
# * global opcode dispatch;
# * backend-wide generated support.
#
# These intentionally retain a dependency on every instruction specification.
ptx_json_array_to_list(
    PTX_RESOLVED_IR_GLOBAL_OUTPUTS
    "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
    global_outputs
)

if(NOT PTX_RESOLVED_IR_GLOBAL_OUTPUTS)
    message(
        FATAL_ERROR
        "Resolved IR generator described no global outputs"
    )
endif()

add_custom_command(
    OUTPUT
    ${PTX_RESOLVED_IR_GLOBAL_OUTPUTS}

    BYPRODUCTS
    "${PTX_RESOLVED_IR_GENERATED_DIR}/.ptx_resolved_ir_outputs.txt"

    COMMAND
    ${Python3_EXECUTABLE}
    "${PTX_RESOLVED_IR_GENERATOR}"

    --spec-dir
    "${PTX_RESOLVED_IR_SPEC_DIR}"

    --backend-spec
    "${PTX_RESOLVED_IR_BACKEND_SPEC}"

    --output
    "${PTX_RESOLVED_IR_GENERATED_DIR}"

    --global-artifacts

    DEPENDS
    ${PTX_RESOLVED_IR_SPEC_FILES}
    ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
    ${PTX_RESOLVED_IR_BACKEND_SPEC}
    ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
    ${PTX_RESOLVED_IR_CODEGEN_FILES}

    COMMENT
    "Generating global resolved-IR artifacts"

    VERBATIM
)

# -----------------------------------------------------------------------------
# Category-local generated artifacts
# -----------------------------------------------------------------------------
string(
    JSON PTX_RESOLVED_IR_CATEGORY_COUNT
    LENGTH
    "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
    categories
)

if(PTX_RESOLVED_IR_CATEGORY_COUNT GREATER 0)
    math(
        EXPR PTX_RESOLVED_IR_CATEGORY_LAST
        "${PTX_RESOLVED_IR_CATEGORY_COUNT} - 1"
    )

    foreach(
        _category_index
        RANGE 0 ${PTX_RESOLVED_IR_CATEGORY_LAST}
    )
        # ---------------------------------------------------------------------
        # Category name
        # ---------------------------------------------------------------------
        string(
            JSON _category
            GET
            "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
            categories
            ${_category_index}
            name
        )

        if(_category STREQUAL "")
            message(
                FATAL_ERROR
                "Resolved IR build description contains an empty category name"
            )
        endif()

        # ---------------------------------------------------------------------
        # Category-owned specification files
        # ---------------------------------------------------------------------
        ptx_json_array_to_list(
            _category_specs
            "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
            categories
            ${_category_index}
            spec_files
        )

        if(NOT _category_specs)
            message(
                FATAL_ERROR
                "Resolved IR category '${_category}' has no specification inputs"
            )
        endif()

        # ---------------------------------------------------------------------
        # Category-owned generated outputs
        # ---------------------------------------------------------------------
        ptx_json_array_to_list(
            _category_outputs
            "${PTX_RESOLVED_IR_BUILD_DESCRIPTION}"
            categories
            ${_category_index}
            outputs
        )

        if(NOT _category_outputs)
            message(
                FATAL_ERROR
                "Resolved IR category '${_category}' has no generated outputs"
            )
        endif()

        # ---------------------------------------------------------------------
        # Convert spec files into repeated:
        #
        # --spec-file <path>
        #
        # CLI arguments.
        # ---------------------------------------------------------------------
        set(_category_spec_arguments)

        foreach(_spec IN LISTS _category_specs)
            list(
                APPEND _category_spec_arguments
                --spec-file
                "${_spec}"
            )
        endforeach()

        # ---------------------------------------------------------------------
        # One independent Ninja edge per codegen category.
        #
        # The Python process sees only the explicit specification subset that
        # contributes to this category.  This is important: merely narrowing
        # CMake DEPENDS while still allowing the generator to read the complete
        # spec tree would create hidden build dependencies.
        # ---------------------------------------------------------------------
        add_custom_command(
            OUTPUT
            ${_category_outputs}

            COMMAND
            ${Python3_EXECUTABLE}
            "${PTX_RESOLVED_IR_GENERATOR}"

            --spec-dir
            "${PTX_RESOLVED_IR_SPEC_DIR}"

            --backend-spec
            "${PTX_RESOLVED_IR_BACKEND_SPEC}"

            --output
            "${PTX_RESOLVED_IR_GENERATED_DIR}"

            --category
            "${_category}"

            ${_category_spec_arguments}

            DEPENDS
            ${_category_specs}
            ${PTX_RESOLVED_IR_INSTRUCTION_SCHEMA}
            ${PTX_RESOLVED_IR_BACKEND_SPEC}
            ${PTX_RESOLVED_IR_BACKEND_SCHEMA}
            ${PTX_RESOLVED_IR_CODEGEN_FILES}

            COMMENT
            "Generating resolved-IR category ${_category}"

            VERBATIM
        )
    endforeach()
endif()

# -----------------------------------------------------------------------------
# Aggregate generation target
# -----------------------------------------------------------------------------

# Every generated output belongs to exactly one custom command:
#
# * one global generation edge, or
# * one category-local generation edge.
#
# resolved_ir_codegen remains the compatibility synchronization target used by
# the native resolved_ir target.
add_custom_target(
    resolved_ir_codegen
    DEPENDS
    ${PTX_RESOLVED_IR_GENERATED_FILES}
)

# -----------------------------------------------------------------------------
# Generated C++ translation units
# -----------------------------------------------------------------------------
set(
    PTX_RESOLVED_IR_GENERATED_SRCS
    ${PTX_RESOLVED_IR_GENERATED_FILES}
)

list(
    FILTER PTX_RESOLVED_IR_GENERATED_SRCS
    INCLUDE REGEX "\\.gen\\.cpp$"
)