if(NOT DEFINED PTX_BINARY_DIR OR NOT DEFINED PTX_FIXTURE OR
   NOT DEFINED PTX_GENERATED_PRIVATE)
    message(FATAL_ERROR
        "PTX_BINARY_DIR, PTX_FIXTURE, and PTX_GENERATED_PRIVATE are required")
endif()

set(_old_partition "${PTX_GENERATED_PRIVATE}/resolved_ir_test.gen.cpp")
set(_new_partition "${PTX_GENERATED_PRIVATE}/resolved_ir_topology.gen.cpp")
file(READ "${PTX_FIXTURE}" _fixture_before)
string(FIND "${_fixture_before}" "codegen_category: test" _category_offset)
if(_category_offset EQUAL -1)
    message(FATAL_ERROR "Expected the synthetic fixture to use codegen_category: test")
endif()

set(_build_command
    "${CMAKE_COMMAND}" --build "${PTX_BINARY_DIR}"
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
file(WRITE "${PTX_FIXTURE}" "${_fixture_after}")
file(REMOVE "${_old_partition}")
file(GLOB_RECURSE _old_partition_objects
    "${PTX_BINARY_DIR}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_test.gen.cpp.o"
    "${PTX_BINARY_DIR}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_test.gen.cpp.obj")
file(REMOVE ${_old_partition_objects})

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
elseif(EXISTS "${_old_partition}")
    string(APPEND _failure
        "Topology-changing direct build retained obsolete ${_old_partition}\n")
endif()

if(_failure STREQUAL "")
    file(GLOB_RECURSE _new_partition_objects
        "${PTX_BINARY_DIR}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_topology.gen.cpp.o"
        "${PTX_BINARY_DIR}/submod/resolved_ir/CMakeFiles/modern_operand_resolved_ir.dir/*/resolved_ir_topology.gen.cpp.obj")
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
    endif()
endif()

file(WRITE "${PTX_FIXTURE}" "${_fixture_before}")
execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _restore_build_result
    OUTPUT_VARIABLE _restore_build_stdout
    ERROR_VARIABLE _restore_build_stderr)
if(NOT _restore_build_result EQUAL 0)
    string(APPEND _failure
        "Fixture restore build failed (${_restore_build_result}):\n"
        "${_restore_build_stdout}${_restore_build_stderr}\n")
endif()

if(NOT _failure STREQUAL "")
    message(FATAL_ERROR "${_failure}")
endif()
