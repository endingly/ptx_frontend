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

# Define the Resolved IR generation graph and return only paths needed by its
# library target. All spec discovery, invalidation and repair state stays local.
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

    # One Ninja category process uses the full artifact-writer budget. This keeps
    # the dominant large category from being stranded with a fraction of workers.
    # Other generators serialize category commands through their stamp chain.
    set(resolved_category_jobs "${PTX_FRONTEND_CODEGEN_JOBS}")
    set(resolved_codegen_pool_option)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS
            "ptx_resolved_codegen=1")
        set(resolved_codegen_pool_option JOB_POOL ptx_resolved_codegen)
    endif()

    set(resolved_shared_codegen_inputs
        "${resolved_instruction_schema}" "${resolved_backend}"
        "${resolved_backend_schema}" ${resolved_codegen_python})
    string(JSON resolved_category_count LENGTH
        "${resolved_build_description}" categories)
    set(resolved_category_stamps)
    set(resolved_previous_category_stamp)
    if(resolved_category_count EQUAL 0)
        message(FATAL_ERROR "Resolved IR build description has no categories")
    endif()
    math(EXPR resolved_last_category "${resolved_category_count} - 1")
    foreach(resolved_index RANGE 0 ${resolved_last_category})
        string(JSON resolved_category GET
            "${resolved_build_description}" categories ${resolved_index} name)
        _ptx_resolved_ir_json_array(resolved_category_specs
            "${resolved_build_description}" categories ${resolved_index} spec_files)
        set(resolved_category_spec_options)
        foreach(resolved_spec IN LISTS resolved_category_specs)
            list(APPEND resolved_category_spec_options --spec-file "${resolved_spec}")
        endforeach()
        _ptx_resolved_ir_json_array(resolved_category_outputs
            "${resolved_build_description}" categories ${resolved_index} outputs)
        set(resolved_category_inputs_manifest
            "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_${resolved_category}_inputs.txt")
        string(JOIN "\n" resolved_category_inputs_text ${resolved_category_specs})
        file(GENERATE OUTPUT "${resolved_category_inputs_manifest}"
            CONTENT "${resolved_category_inputs_text}\n")
        set(resolved_category_stamp
            "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_${resolved_category}.stamp")
        set(resolved_category_dependencies
            ${resolved_category_specs} "${resolved_category_inputs_manifest}"
            ${resolved_shared_codegen_inputs})
        if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND resolved_previous_category_stamp)
            list(APPEND resolved_category_dependencies
                "${resolved_previous_category_stamp}")
        endif()
        add_custom_command(
            OUTPUT "${resolved_category_stamp}"
            BYPRODUCTS ${resolved_category_outputs}
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${resolved_category_stamp}"
            COMMAND ${resolved_codegen_base_command}
                --category "${resolved_category}"
                ${resolved_category_spec_options}
                --jobs "${resolved_category_jobs}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${resolved_category_stamp}"
            DEPENDS ${resolved_category_dependencies}
            ${resolved_codegen_pool_option}
            COMMENT "Generating resolved IR category ${resolved_category}"
            VERBATIM)
        list(APPEND resolved_category_stamps "${resolved_category_stamp}")
        set(resolved_previous_category_stamp "${resolved_category_stamp}")
    endforeach()

    _ptx_resolved_ir_json_array(resolved_global_outputs
        "${resolved_build_description}" global_outputs)
    set(resolved_global_stamp
        "${CMAKE_CURRENT_BINARY_DIR}/resolved_codegen_global.stamp")
    set(resolved_codegen_preflight_command)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        # Check missing Makefile byproducts before global cleanup can publish a
        # manifest, even when a spec edit already made the global stamp dirty.
        set(resolved_codegen_repair_script
            "${CMAKE_CURRENT_BINARY_DIR}/repair_codegen_outputs.cmake")
        configure_file(
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/repair_codegen_outputs.cmake.in"
            "${resolved_codegen_repair_script}" @ONLY)
        set(resolved_codegen_preflight_command
            COMMAND "${CMAKE_COMMAND}" -P "${resolved_codegen_repair_script}")
    endif()
    add_custom_command(
        OUTPUT "${resolved_global_stamp}"
        BYPRODUCTS ${resolved_global_outputs}
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${resolved_global_stamp}"
        ${resolved_codegen_preflight_command}
        COMMAND ${resolved_codegen_base_command}
            --global-artifacts --jobs "${PTX_FRONTEND_CODEGEN_JOBS}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${resolved_global_stamp}"
        DEPENDS ${resolved_category_stamps} ${resolved_spec_files}
            "${resolved_spec_inputs_manifest}" ${resolved_shared_codegen_inputs}
        ${resolved_codegen_pool_option}
        COMMENT "Generating shared resolved IR artifacts and finalizing outputs"
        VERBATIM)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        add_custom_target(resolved_ir_codegen
            DEPENDS "${resolved_global_stamp}")
    else()
        # Makefile generators do not inspect missing BYPRODUCTS when the stamp
        # remains present. This target check covers otherwise clean global stamps.
        add_custom_target(resolved_ir_codegen
            COMMAND "${CMAKE_COMMAND}" -DPTX_REPAIR_FINALIZE=ON
                -P "${resolved_codegen_repair_script}"
            DEPENDS "${resolved_global_stamp}"
            VERBATIM)
    endif()

    set(${output_sources} "${resolved_generated_sources}" PARENT_SCOPE)
    set(${output_private} "${resolved_generated_private}" PARENT_SCOPE)
    set(${output_public} "${resolved_generated_public}" PARENT_SCOPE)
endfunction()
