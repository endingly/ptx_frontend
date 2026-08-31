if(NOT DEFINED PTX_SOURCE_DIR OR NOT DEFINED PTX_BINARY_DIR OR
   NOT DEFINED PTX_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "PTX_SOURCE_DIR, PTX_BINARY_DIR, and PTX_PYTHON_EXECUTABLE are required")
endif()

set(_test_root "${PTX_BINARY_DIR}/test/package_consumer")
set(_install_dir "${_test_root}/install")
set(_build_dir "${_test_root}/build")
set(_missing_component_build_dir "${_test_root}/missing_component")
set(_missing_spec_install_dir "${_test_root}/missing_spec_install")
set(_missing_spec_build_dir "${_test_root}/missing_spec")
set(_missing_codegen_install_dir "${_test_root}/missing_codegen_install")
set(_missing_codegen_build_dir "${_test_root}/missing_codegen")
set(_relocated_install_dir "${_test_root}/relocated/install")
set(_relocated_source_dir "${_test_root}/relocated/consumer")
set(_relocated_build_dir "${_test_root}/relocated/build")
set(_nondefault_build_dir "${_test_root}/nondefault/build")
set(_nondefault_install_dir "${_test_root}/nondefault/install")
set(_nondefault_consumer_build_dir "${_test_root}/nondefault/consumer")
file(REMOVE_RECURSE
    "${_install_dir}"
    "${_build_dir}"
    "${_missing_component_build_dir}"
    "${_missing_spec_install_dir}"
    "${_missing_spec_build_dir}"
    "${_missing_codegen_install_dir}"
    "${_missing_codegen_build_dir}"
    "${_relocated_install_dir}"
    "${_relocated_source_dir}"
    "${_relocated_build_dir}"
    "${_nondefault_build_dir}"
    "${_nondefault_install_dir}"
    "${_nondefault_consumer_build_dir}"
)

set(_install_command
    "${CMAKE_COMMAND}" --install "${PTX_BINARY_DIR}" --prefix "${_install_dir}"
)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _install_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_install_command} COMMAND_ERROR_IS_FATAL ANY)

foreach(_header IN ITEMS
        "${_install_dir}/include/resolved_ir.gen.hpp"
        "${_install_dir}/include/ptx_frontend/common/source_loc.hpp"
        "${_install_dir}/include/ptx_frontend/base/base.hpp"
        "${_install_dir}/include/ptx_frontend/lexer/ptx_lexer.hpp"
        "${_install_dir}/include/ptx_frontend/cst/ptx_cst.hpp"
        "${_install_dir}/include/ptx_frontend/syntax/ptx_syntax_ast.hpp"
        "${_install_dir}/include/ptx_frontend/binding/ptx_symbol_table.hpp"
        "${_install_dir}/include/ptx_frontend/semantic/ptx_call_argument_compatibility.hpp"
        "${_install_dir}/include/ptx_frontend/semantic/ptx_declaration_semantics.hpp"
        "${_install_dir}/include/ptx_frontend/resolved_ir/ptx_resolved_ir.hpp")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR "Public header was not installed: ${_header}")
    endif()
endforeach()

set(_codegen_python_root
    "${_install_dir}/share/ptx_frontend/codegen/python")
foreach(_resource IN ITEMS
        "${_codegen_python_root}/code_gen/resources/ptx_spec/arithmetic.yaml"
        "${_codegen_python_root}/code_gen/resources/ptx-instr-v1.schema.yaml"
        "${_codegen_python_root}/scripts/gen_all.py"
        "${_codegen_python_root}/code_gen/resources/ptx-cpp-backend-v1.schema.yaml")
    if(NOT EXISTS "${_resource}")
        message(FATAL_ERROR "Required component resource was not installed: ${_resource}")
    endif()
endforeach()

if(EXISTS "${_install_dir}/share/ptx_frontend/spec")
    message(FATAL_ERROR
        "PTX spec was duplicated outside the codegen Python package resources")
endif()

file(GLOB_RECURSE _installed_backend_specs
    "${_install_dir}/*ptx_cpp_backend_spec*"
    "${_install_dir}/*ptx_frontend.yaml")
if(_installed_backend_specs)
    message(FATAL_ERROR
        "Private backend specification leaked into install tree: "
        "${_installed_backend_specs}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${_codegen_python_root}"
        "${PTX_PYTHON_EXECUTABLE}" -c
        "from code_gen.cpp_backend import get_cpp_backend; get_cpp_backend()"
    RESULT_VARIABLE _unconfigured_backend_result
    ERROR_VARIABLE _unconfigured_backend_error
)
if(_unconfigured_backend_result EQUAL 0 OR NOT _unconfigured_backend_error MATCHES
       "C\\+\\+ backend is not configured")
    message(FATAL_ERROR "Installed Python API did not reject an unconfigured backend")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${_codegen_python_root}"
        "${PTX_PYTHON_EXECUTABLE}" -c
        "from pathlib import Path; from code_gen.cpp_backend import configure_cpp_backend, get_cpp_backend; configure_cpp_backend(Path(r'${PTX_SOURCE_DIR}/test/package_consumer/backend.yaml')); get_cpp_backend()"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${_codegen_python_root}"
        "${PTX_PYTHON_EXECUTABLE}" -m code_gen --help
    COMMAND_ERROR_IS_FATAL ANY
)

set(_consumer_common_args
    "-DCMAKE_PREFIX_PATH=${_install_dir}"
)
if(DEFINED PTX_TOOLCHAIN_FILE AND NOT PTX_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _consumer_common_args
         "-DCMAKE_TOOLCHAIN_FILE=${PTX_TOOLCHAIN_FILE}")
endif()
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _consumer_common_args "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _consumer_common_args
         "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()

set(_configure_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_build_dir}"
    ${_consumer_common_args}
)
execute_process(COMMAND ${_configure_command} COMMAND_ERROR_IS_FATAL ANY)

set(_build_command "${CMAKE_COMMAND}" --build "${_build_dir}")
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_build_command} COMMAND_ERROR_IS_FATAL ANY)

if(NOT EXISTS "${_build_dir}/generated/public/resolved_ir.gen.hpp")
    message(FATAL_ERROR "Installed codegen did not generate resolved_ir.gen.hpp")
endif()
if(NOT EXISTS "${_build_dir}/generated_secondary/public/resolved_ir.gen.hpp")
    message(FATAL_ERROR "Second installed codegen invocation did not run")
endif()

# Exercise a complete copied prefix and fixture source outside both trees.
file(COPY "${_install_dir}/" DESTINATION "${_relocated_install_dir}")
file(COPY "${PTX_SOURCE_DIR}/test/package_consumer/" DESTINATION "${_relocated_source_dir}")
set(_relocated_configure_command
    "${CMAKE_COMMAND}" -S "${_relocated_source_dir}" -B "${_relocated_build_dir}"
    "-DCMAKE_PREFIX_PATH=${_relocated_install_dir}")
if(DEFINED PTX_TOOLCHAIN_FILE AND NOT PTX_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _relocated_configure_command
        "-DCMAKE_TOOLCHAIN_FILE=${PTX_TOOLCHAIN_FILE}")
endif()
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _relocated_configure_command "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _relocated_configure_command
        "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
execute_process(COMMAND ${_relocated_configure_command} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_relocated_build_dir}"
    COMMAND_ERROR_IS_FATAL ANY)

# PATH_VARS must also preserve a non-default GNUInstallDirs data location.
get_filename_component(_project_source_dir "${PTX_SOURCE_DIR}/../.." ABSOLUTE)
set(_nondefault_configure_command
    "${CMAKE_COMMAND}" -S "${_project_source_dir}" -B "${_nondefault_build_dir}"
    "-DBUILD_TESTING=OFF"
    "-DPTX_USE_CCACHE=OFF"
    "-DCMAKE_INSTALL_DATADIR=custom-data")
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _nondefault_configure_command "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _nondefault_configure_command
        "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
execute_process(COMMAND ${_nondefault_configure_command} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_nondefault_build_dir}"
    --target resolved_ir COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${_nondefault_build_dir}"
    --prefix "${_nondefault_install_dir}" COMMAND_ERROR_IS_FATAL ANY)
set(_nondefault_consumer_command
    "${CMAKE_COMMAND}" -S "${_relocated_source_dir}" -B "${_nondefault_consumer_build_dir}"
    "-DCMAKE_PREFIX_PATH=${_nondefault_install_dir}")
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _nondefault_consumer_command "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _nondefault_consumer_command "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
execute_process(COMMAND ${_nondefault_consumer_command} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_nondefault_consumer_build_dir}"
    COMMAND_ERROR_IS_FATAL ANY)

# The installed helper must both rerun generation for a known input change and
# reconfigure when that change adds a generated output category.
set(_generated_header "${_build_dir}/generated/public/resolved_ir.gen.hpp")
file(TIMESTAMP "${_generated_header}" _header_before)
file(READ "${_codegen_python_root}/code_gen/resources/ptx_spec/arithmetic.yaml"
     _arithmetic_spec)
string(REPLACE "codegen_category: arithmetic"
    "codegen_category: arithmetic_topology" _arithmetic_spec
    "${_arithmetic_spec}")
file(WRITE "${_codegen_python_root}/code_gen/resources/ptx_spec/arithmetic.yaml"
    "${_arithmetic_spec}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
execute_process(COMMAND ${_build_command} COMMAND_ERROR_IS_FATAL ANY)
if(NOT EXISTS "${_build_dir}/generated/private/resolved_ir_arithmetic_topology.gen.cpp")
    message(FATAL_ERROR "Installed codegen did not discover changed output topology")
endif()
file(TIMESTAMP "${_generated_header}" _header_after)
if("${_header_before}" STREQUAL "${_header_after}")
    message(FATAL_ERROR "Installed codegen did not rerun after a PTX spec change")
endif()

set(_test_command
    "${CMAKE_CTEST_COMMAND}" --test-dir "${_build_dir}" --output-on-failure
)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _test_command --build-config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_test_command} COMMAND_ERROR_IS_FATAL ANY)

set(_missing_component_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_missing_component_build_dir}"
    ${_consumer_common_args}
    -DPTX_FRONTEND_TEST_UNKNOWN_COMPONENT=ON
)
execute_process(
    COMMAND ${_missing_component_command}
    RESULT_VARIABLE _missing_component_result
    OUTPUT_VARIABLE _missing_component_stdout
    ERROR_VARIABLE _missing_component_stderr
)
if(_missing_component_result EQUAL 0)
    message(FATAL_ERROR
        "find_package(ptx_frontend COMPONENTS definitely_unknown) "
        "unexpectedly succeeded")
endif()
string(CONCAT _missing_component_output
       "${_missing_component_stdout}" "${_missing_component_stderr}")
if(NOT _missing_component_output MATCHES
       "Unsupported ptx_frontend component\\(s\\): definitely_unknown")
    message(FATAL_ERROR
        "Missing-component configure failed without the expected diagnostic:\n"
        "${_missing_component_output}")
endif()

file(COPY "${_install_dir}/" DESTINATION "${_missing_spec_install_dir}")
file(REMOVE_RECURSE
    "${_missing_spec_install_dir}/share/ptx_frontend/codegen/python/code_gen/resources/ptx_spec")
set(_missing_spec_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_missing_spec_build_dir}"
    "-DCMAKE_PREFIX_PATH=${_missing_spec_install_dir}"
    -DPTX_FRONTEND_TEST_PTX_SPEC_ONLY=ON
)
execute_process(
    COMMAND ${_missing_spec_command}
    RESULT_VARIABLE _missing_spec_result
    OUTPUT_VARIABLE _missing_spec_stdout
    ERROR_VARIABLE _missing_spec_stderr
)
if(_missing_spec_result EQUAL 0)
    message(FATAL_ERROR
        "find_package(ptx_frontend REQUIRED COMPONENTS ptx_spec) succeeded "
        "after the installed PTX spec directory was removed")
endif()

file(COPY "${_install_dir}/" DESTINATION "${_missing_codegen_install_dir}")
file(REMOVE "${_missing_codegen_install_dir}/share/ptx_frontend/codegen/python/code_gen/__init__.py")
set(_missing_codegen_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_missing_codegen_build_dir}"
    "-DCMAKE_PREFIX_PATH=${_missing_codegen_install_dir}"
    -DPTX_FRONTEND_TEST_CODEGEN_ONLY=ON
)
execute_process(
    COMMAND ${_missing_codegen_command}
    RESULT_VARIABLE _missing_codegen_result
    OUTPUT_VARIABLE _missing_codegen_stdout
    ERROR_VARIABLE _missing_codegen_stderr
)
if(_missing_codegen_result EQUAL 0)
    message(FATAL_ERROR
        "find_package(ptx_frontend REQUIRED COMPONENTS codegen) succeeded "
        "after an essential codegen package file was removed")
endif()
