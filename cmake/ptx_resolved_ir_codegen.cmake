include_guard(GLOBAL)

# Read one array in the generator's authoritative JSON build description.
function(_ptx_resolved_ir_json_array output description)
    string(JSON item_count LENGTH "${description}" ${ARGN})
    set(items)
    if(item_count GREATER 0)
        math(EXPR last_item "${item_count} - 1")
        foreach(item_index RANGE 0 ${last_item})
            string(JSON item GET "${description}" ${ARGN} ${item_index})
            list(APPEND items "${item}")
        endforeach()
    endif()
    set(${output} "${items}" PARENT_SCOPE)
endfunction()

# Define one codegen command per build invocation and return library paths.
# Per-category stamps are build state, while the aggregate edge owns all outputs.
function(ptx_configure_resolved_ir_codegen output_sources output_private output_public)
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

    file(GLOB_RECURSE resolved_spec_files CONFIGURE_DEPENDS
        "${resolved_spec_dir}/*.yaml")
    list(FILTER resolved_spec_files EXCLUDE REGEX "\\.schema\\.yaml$")
    set(resolved_spec_inputs_manifest
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_spec_inputs.txt")
    string(JOIN "\n" resolved_spec_inputs_text ${resolved_spec_files})
    file(GENERATE OUTPUT "${resolved_spec_inputs_manifest}"
        CONTENT "${resolved_spec_inputs_text}\n")

    file(GLOB_RECURSE resolved_codegen_python CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/base/*.py"
        "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/code_gen/*.py"
        "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/*.py"
        "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/*.py")
    set(resolved_codegen_python_membership
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_python_inputs.txt")
    string(JOIN "\n" resolved_codegen_python_inputs_text
        ${resolved_codegen_python})
    file(GENERATE OUTPUT "${resolved_codegen_python_membership}"
        CONTENT "${resolved_codegen_python_inputs_text}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${resolved_spec_files}
        "${resolved_instruction_schema}"
        "${resolved_backend}"
        "${resolved_backend_schema}"
        ${resolved_codegen_python})

    set(resolved_codegen_base_command
        "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PROJECT_SOURCE_DIR}/python/src"
        "${Python3_EXECUTABLE}" -m ptx_frontend.code_gen
        --spec-dir "${resolved_spec_dir}"
        --backend-spec "${resolved_backend}"
        --output "${resolved_generated_root}")
    execute_process(
        COMMAND ${resolved_codegen_base_command} --describe-build
        OUTPUT_VARIABLE resolved_build_description
        ERROR_VARIABLE resolved_codegen_error
        RESULT_VARIABLE resolved_codegen_result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT resolved_codegen_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to discover resolved IR outputs: ${resolved_codegen_error}")
    endif()

    _ptx_resolved_ir_json_array(resolved_generated_outputs
        "${resolved_build_description}" all_outputs)
    set(resolved_generated_sources ${resolved_generated_outputs})
    list(FILTER resolved_generated_sources INCLUDE REGEX "\\.gen\\.cpp$")
    set(resolved_build_description_file
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_build.json")
    file(GENERATE OUTPUT "${resolved_build_description_file}"
        CONTENT "${resolved_build_description}\n")

    # The runner compares contributor and membership mtimes with these stamps.
    # Their absence is also a Ninja byproduct invalidation signal.
    string(JSON resolved_category_count LENGTH
        "${resolved_build_description}" categories)
    if(resolved_category_count EQUAL 0)
        message(FATAL_ERROR "Resolved IR build description has no categories")
    endif()
    set(resolved_category_stamps)
    set(resolved_category_membership_manifests)
    math(EXPR resolved_last_category "${resolved_category_count} - 1")
    foreach(resolved_index RANGE 0 ${resolved_last_category})
        string(JSON resolved_category GET
            "${resolved_build_description}" categories ${resolved_index} name)
        _ptx_resolved_ir_json_array(resolved_category_specs
            "${resolved_build_description}" categories ${resolved_index} spec_files)
        set(resolved_category_inputs_manifest
            "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_${resolved_category}_inputs.txt")
        string(JOIN "\n" resolved_category_inputs_text ${resolved_category_specs})
        file(GENERATE OUTPUT "${resolved_category_inputs_manifest}"
            CONTENT "${resolved_category_inputs_text}\n")
        list(APPEND resolved_category_membership_manifests
            "${resolved_category_inputs_manifest}")
        list(APPEND resolved_category_stamps
            "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_${resolved_category}.stamp")
    endforeach()

    set(resolved_codegen_batch_script
        "${CMAKE_CURRENT_BINARY_DIR}/run_resolved_codegen_batch.cmake")
    set(resolved_shared_codegen_inputs
        "${resolved_instruction_schema}" "${resolved_backend}"
        "${resolved_backend_schema}" ${resolved_codegen_python}
        "${resolved_codegen_python_membership}"
        "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/run_resolved_codegen_batch.cmake.in")
    set(resolved_global_stamp
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_global.stamp")
    set(resolved_aggregate_stamp
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_complete.stamp")
    set(resolved_output_manifest
        "${resolved_generated_root}/.ptx_resolved_ir_outputs.txt")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/run_resolved_codegen_batch.cmake.in"
        "${resolved_codegen_batch_script}" @ONLY)

    set(resolved_codegen_pool_option)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS "ptx_resolved_codegen=1")
        set(resolved_codegen_pool_option JOB_POOL ptx_resolved_codegen)
    endif()
    add_custom_command(
        OUTPUT "${resolved_aggregate_stamp}"
        BYPRODUCTS ${resolved_generated_outputs} "${resolved_output_manifest}"
            ${resolved_category_stamps} "${resolved_global_stamp}"
        COMMAND "${CMAKE_COMMAND}" -DPTX_ACKNOWLEDGE_EDGE=ON
            -P "${resolved_codegen_batch_script}"
        DEPENDS ${resolved_spec_files} "${resolved_spec_inputs_manifest}"
            ${resolved_category_membership_manifests}
            "${resolved_build_description_file}"
            "${resolved_codegen_batch_script}"
            ${resolved_shared_codegen_inputs}
        ${resolved_codegen_pool_option}
        COMMENT "Generating changed resolved IR categories and shared artifacts"
        VERBATIM)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        add_custom_target(resolved_ir_codegen
            DEPENDS "${resolved_aggregate_stamp}")
    else()
        # Makefile generators do not inspect missing BYPRODUCTS of a clean stamp.
        add_custom_target(resolved_ir_codegen
            COMMAND "${CMAKE_COMMAND}" -P "${resolved_codegen_batch_script}"
            DEPENDS "${resolved_aggregate_stamp}"
            VERBATIM)
    endif()

    set(${output_sources} "${resolved_generated_sources}" PARENT_SCOPE)
    set(${output_private} "${resolved_generated_private}" PARENT_SCOPE)
    set(${output_public} "${resolved_generated_public}" PARENT_SCOPE)
endfunction()
