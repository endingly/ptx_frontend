if(NOT DEFINED PTX_SOURCE_DIR OR NOT DEFINED PTX_FIXTURE OR
   NOT DEFINED PTX_TEST_ROOT)
    message(FATAL_ERROR
        "PTX_SOURCE_DIR, PTX_FIXTURE, and PTX_TEST_ROOT are required")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _test_run_id)
set(_test_run_root "${PTX_TEST_ROOT}/${_test_run_id}")
set(_fixture_dir "${_test_run_root}/fixture")
set(_nested_binary_dir "${_test_run_root}/build")
set(_nested_generated_private
    "${_nested_binary_dir}/submod/resolved_ir/test_generated/private")
set(_old_partition "${_nested_generated_private}/resolved_ir_test.gen.cpp")
set(_new_partition "${_nested_generated_private}/resolved_ir_topology.gen.cpp")

file(MAKE_DIRECTORY "${_fixture_dir}")
file(COPY_FILE "${PTX_FIXTURE}" "${_fixture_dir}/modern_operand.yaml")
set(_fixture "${_fixture_dir}/modern_operand.yaml")

set(_configure_command
    "${CMAKE_COMMAND}" -S "${PTX_SOURCE_DIR}" -B "${_nested_binary_dir}"
    "-DBUILD_TESTING=ON"
    "-DPTX_MODERN_OPERAND_TEST_SPEC_DIR:PATH=${_fixture_dir}")
if(DEFINED PTX_CMAKE_GENERATOR AND NOT PTX_CMAKE_GENERATOR STREQUAL "")
    list(APPEND _configure_command -G "${PTX_CMAKE_GENERATOR}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_PLATFORM AND
   NOT PTX_CMAKE_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _configure_command -A "${PTX_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_TOOLSET AND
   NOT PTX_CMAKE_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _configure_command -T "${PTX_CMAKE_GENERATOR_TOOLSET}")
endif()
foreach(_cache_var
        CMAKE_MAKE_PROGRAM
        CMAKE_BUILD_TYPE
        CMAKE_TOOLCHAIN_FILE
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        fmt_DIR
        magic_enum_DIR)
    set(_forward_var "PTX_${_cache_var}")
    if(DEFINED ${_forward_var} AND NOT "${${_forward_var}}" STREQUAL "")
        list(APPEND _configure_command
            "-D${_cache_var}:STRING=${${_forward_var}}")
    endif()
endforeach()

execute_process(
    COMMAND ${_configure_command}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_stdout
    ERROR_VARIABLE _configure_stderr)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
        "Nested topology test configure failed (${_configure_result}):\n"
        "${_configure_stdout}${_configure_stderr}")
endif()

file(READ "${PTX_FIXTURE}" _fixture_before)
string(FIND "${_fixture_before}" "codegen_category: test" _category_offset)
if(_category_offset EQUAL -1)
    message(FATAL_ERROR "Expected the synthetic fixture to use codegen_category: test")
endif()

set(_build_command
    "${CMAKE_COMMAND}" --build "${_nested_binary_dir}"
    --target modern_operand_resolved_ir)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${PTX_TEST_CONFIG}")
endif()

execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _initial_build_result
    OUTPUT_VARIABLE _initial_build_stdout
    ERROR_VARIABLE _initial_build_stderr)
if(NOT _initial_build_result EQUAL 0)
    message(FATAL_ERROR
        "Initial topology test build failed (${_initial_build_result}):\n"
        "${_initial_build_stdout}${_initial_build_stderr}")
endif()

if(NOT EXISTS "${_old_partition}")
    message(FATAL_ERROR "Expected initial generated partition: ${_old_partition}")
endif()

string(REPLACE "codegen_category: test" "codegen_category: topology"
    _fixture_after "${_fixture_before}")
file(WRITE "${_fixture}" "${_fixture_after}")
file(GLOB_RECURSE _old_partition_objects
    "${_nested_binary_dir}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_test.gen.cpp.o"
    "${_nested_binary_dir}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_test.gen.cpp.obj")

execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _topology_build_result
    OUTPUT_VARIABLE _topology_build_stdout
    ERROR_VARIABLE _topology_build_stderr)

set(_failure "")
if(NOT _topology_build_result EQUAL 0)
    string(APPEND _failure
        "Topology-changing direct build failed (${_topology_build_result}):\n"
        "${_topology_build_stdout}${_topology_build_stderr}\n")
elseif(NOT EXISTS "${_new_partition}")
    string(APPEND _failure
        "Topology-changing direct build did not generate ${_new_partition}\n")
endif()

if(_failure STREQUAL "")
    file(GLOB_RECURSE _new_partition_objects
        "${_nested_binary_dir}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_topology.gen.cpp.o"
        "${_nested_binary_dir}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_topology.gen.cpp.obj")
    list(LENGTH _new_partition_objects _new_partition_object_count)
    if(_new_partition_object_count EQUAL 0)
        string(APPEND _failure
            "Topology-changing direct build did not compile ${_new_partition}\n")
    endif()
endif()

if(_failure STREQUAL "")
    list(GET _new_partition_objects 0 _new_partition_object)
    file(TIMESTAMP "${_new_partition}" _partition_timestamp_before)
    file(TIMESTAMP "${_new_partition_object}" _object_timestamp_before)
    file(REMOVE "${_old_partition}" ${_old_partition_objects})
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
    execute_process(
        COMMAND ${_build_command}
        RESULT_VARIABLE _noop_build_result
        OUTPUT_VARIABLE _noop_build_stdout
        ERROR_VARIABLE _noop_build_stderr)
    file(TIMESTAMP "${_new_partition}" _partition_timestamp_after)
    file(TIMESTAMP "${_new_partition_object}" _object_timestamp_after)
    if(NOT _noop_build_result EQUAL 0)
        string(APPEND _failure
            "Unchanged direct build failed (${_noop_build_result}):\n"
            "${_noop_build_stdout}${_noop_build_stderr}\n")
    elseif(NOT "${_partition_timestamp_before}" STREQUAL
           "${_partition_timestamp_after}")
        string(APPEND _failure
            "Unchanged direct build regenerated ${_new_partition}\n")
    elseif(NOT "${_object_timestamp_before}" STREQUAL
           "${_object_timestamp_after}")
        string(APPEND _failure
            "Unchanged direct build recompiled ${_new_partition}:\n"
            "${_noop_build_stdout}${_noop_build_stderr}\n")
    elseif(EXISTS "${_old_partition}")
        string(APPEND _failure
            "Unchanged direct build restored obsolete ${_old_partition}\n")
    endif()
endif()

if(NOT _failure STREQUAL "")
    message(FATAL_ERROR "${_failure}")
endif()

file(REMOVE_RECURSE "${_test_run_root}")
