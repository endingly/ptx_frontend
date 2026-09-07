if(NOT DEFINED PTX_SOURCE_DIR OR NOT DEFINED PTX_BINARY_DIR OR
   NOT DEFINED PTX_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "PTX_SOURCE_DIR, PTX_BINARY_DIR, and PTX_PYTHON_EXECUTABLE are required")
endif()

set(_test_root "${PTX_BINARY_DIR}/test/package_consumer")
set(_install_dir "${_test_root}/install")
set(_build_dir "${_test_root}/build")
set(_missing_component_build_dir "${_test_root}/missing_component")
set(_removed_codegen_build_dir "${_test_root}/removed_codegen")
set(_missing_spec_install_dir "${_test_root}/missing_spec_install")
set(_missing_spec_build_dir "${_test_root}/missing_spec")
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
    "${_removed_codegen_build_dir}"
    "${_missing_spec_install_dir}"
    "${_missing_spec_build_dir}"
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

foreach(_resource IN ITEMS
        "${_install_dir}/share/ptx_frontend/ptx_spec/arithmetic.yaml"
        "${_install_dir}/share/ptx_frontend/ptx-instr-v1.schema.yaml")
    if(NOT EXISTS "${_resource}")
        message(FATAL_ERROR "Required PTX spec resource was not installed: ${_resource}")
    endif()
endforeach()

if(EXISTS "${_install_dir}/lib/cmake/ptx_frontend/ptx_frontendCodegen.cmake")
    message(FATAL_ERROR "Installed package unexpectedly exports the codegen helper")
endif()

file(GLOB_RECURSE _installed_backend_specs
    "${_install_dir}/*ptx_cpp_backend_spec*"
    "${_install_dir}/*ptx_frontend.yaml")
if(_installed_backend_specs)
    message(FATAL_ERROR
        "Private backend specification leaked into install tree: "
        "${_installed_backend_specs}")
endif()

set(_nested_generator_args)
if(DEFINED PTX_CMAKE_GENERATOR AND NOT PTX_CMAKE_GENERATOR STREQUAL "")
    list(APPEND _nested_generator_args -G "${PTX_CMAKE_GENERATOR}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_PLATFORM AND NOT PTX_CMAKE_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _nested_generator_args -A "${PTX_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_TOOLSET AND NOT PTX_CMAKE_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _nested_generator_args -T "${PTX_CMAKE_GENERATOR_TOOLSET}")
endif()
foreach(_cache_spec IN ITEMS
        "CMAKE_MAKE_PROGRAM:FILEPATH"
        "CMAKE_BUILD_TYPE:STRING"
        "CMAKE_C_COMPILER:FILEPATH"
        "CMAKE_CXX_COMPILER:FILEPATH"
        "CMAKE_C_COMPILER_LAUNCHER:STRING"
        "CMAKE_CXX_COMPILER_LAUNCHER:STRING")
    string(REPLACE ":" ";" _cache_spec_parts "${_cache_spec}")
    list(GET _cache_spec_parts 0 _cache_var)
    list(GET _cache_spec_parts 1 _cache_type)
    set(_forward_var "PTX_${_cache_var}")
    if(DEFINED ${_forward_var} AND NOT "${${_forward_var}}" STREQUAL "")
        list(APPEND _nested_generator_args
            "-D${_cache_var}:${_cache_type}=${${_forward_var}}")
    endif()
endforeach()

set(_consumer_dependency_args)
if(DEFINED PTX_TOOLCHAIN_FILE AND NOT PTX_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _consumer_dependency_args
        "-DCMAKE_TOOLCHAIN_FILE=${PTX_TOOLCHAIN_FILE}")
endif()
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _consumer_dependency_args "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _consumer_dependency_args
        "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()

set(_consumer_common_args
    "-DCMAKE_PREFIX_PATH=${_install_dir}"
    ${_consumer_dependency_args}
)

set(_configure_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_build_dir}"
    ${_nested_generator_args}
    ${_consumer_common_args}
)
execute_process(COMMAND ${_configure_command} COMMAND_ERROR_IS_FATAL ANY)

set(_build_command "${CMAKE_COMMAND}" --build "${_build_dir}" --parallel)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_build_command} COMMAND_ERROR_IS_FATAL ANY)

set(_test_command
    "${CMAKE_CTEST_COMMAND}" --test-dir "${_build_dir}" --output-on-failure
)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _test_command --build-config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_test_command} COMMAND_ERROR_IS_FATAL ANY)

# Exercise a complete copied prefix and fixture source outside both trees.
file(COPY "${_install_dir}/" DESTINATION "${_relocated_install_dir}")
file(COPY "${PTX_SOURCE_DIR}/test/package_consumer/" DESTINATION "${_relocated_source_dir}")
set(_relocated_configure_command
    "${CMAKE_COMMAND}" -S "${_relocated_source_dir}" -B "${_relocated_build_dir}"
    ${_nested_generator_args}
    "-DCMAKE_PREFIX_PATH=${_relocated_install_dir}"
    ${_consumer_dependency_args})
execute_process(COMMAND ${_relocated_configure_command} COMMAND_ERROR_IS_FATAL ANY)
set(_relocated_build_command
    "${CMAKE_COMMAND}" --build "${_relocated_build_dir}" --parallel)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _relocated_build_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_relocated_build_command} COMMAND_ERROR_IS_FATAL ANY)

# PATH_VARS must preserve a non-default GNUInstallDirs data location.
get_filename_component(_project_source_dir "${PTX_SOURCE_DIR}/../.." ABSOLUTE)
set(_nondefault_configure_command
    "${CMAKE_COMMAND}" -S "${_project_source_dir}" -B "${_nondefault_build_dir}"
    ${_nested_generator_args}
    "-DBUILD_TESTING=OFF"
    "-DCMAKE_INSTALL_DATADIR=custom-data"
    "-DPython3_EXECUTABLE=${PTX_PYTHON_EXECUTABLE}")
if(DEFINED PTX_TOOLCHAIN_FILE AND NOT PTX_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _nondefault_configure_command
        "-DCMAKE_TOOLCHAIN_FILE=${PTX_TOOLCHAIN_FILE}")
endif()
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _nondefault_configure_command "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _nondefault_configure_command
        "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
execute_process(COMMAND ${_nondefault_configure_command} COMMAND_ERROR_IS_FATAL ANY)

# Reuse the default prefix's compiled targets. The custom-data configure only
# needs to install ptx_spec and replace the package config that records its
# relocated data path.
file(COPY "${_install_dir}/" DESTINATION "${_nondefault_install_dir}")
file(REMOVE_RECURSE
    "${_nondefault_install_dir}/share/ptx_frontend/ptx_spec")
file(REMOVE "${_nondefault_install_dir}/share/ptx_frontend/ptx-instr-v1.schema.yaml")
set(_nondefault_install_command
    "${CMAKE_COMMAND}" --install "${_nondefault_build_dir}"
    --prefix "${_nondefault_install_dir}" --component ptx_spec)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _nondefault_install_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_nondefault_install_command} COMMAND_ERROR_IS_FATAL ANY)

set(_nondefault_package_config
    "${_nondefault_build_dir}/ptx_frontendConfig.cmake")
set(_nondefault_installed_package_config
    "${_nondefault_install_dir}/lib/cmake/ptx_frontend/ptx_frontendConfig.cmake")
if(NOT EXISTS "${_nondefault_package_config}")
    message(FATAL_ERROR "Custom-data package config was not generated")
endif()
file(READ "${_nondefault_package_config}" _nondefault_package_config_content)
if(NOT _nondefault_package_config_content MATCHES
       "custom-data/ptx_frontend/ptx_spec")
    message(FATAL_ERROR "Custom-data package config does not record the PTX spec path")
endif()
file(COPY_FILE "${_nondefault_package_config}"
    "${_nondefault_installed_package_config}" ONLY_IF_DIFFERENT)

if(NOT EXISTS "${_nondefault_install_dir}/custom-data/ptx_frontend/ptx_spec/arithmetic.yaml")
    message(FATAL_ERROR "Non-default PTX spec install location is missing")
endif()
if(EXISTS "${_nondefault_install_dir}/share/ptx_frontend/ptx_spec" OR
   EXISTS "${_nondefault_install_dir}/share/ptx_frontend/ptx-instr-v1.schema.yaml")
    message(FATAL_ERROR "Non-default install retained default PTX spec resources")
endif()
set(_nondefault_consumer_command
    "${CMAKE_COMMAND}" -S "${_relocated_source_dir}" -B "${_nondefault_consumer_build_dir}"
    ${_nested_generator_args}
    "-DCMAKE_PREFIX_PATH=${_nondefault_install_dir}"
    "-DPTX_FRONTEND_TEST_EXPECTED_PTX_SPEC_DIR=${_nondefault_install_dir}/custom-data/ptx_frontend/ptx_spec"
    ${_consumer_dependency_args})
execute_process(COMMAND ${_nondefault_consumer_command} COMMAND_ERROR_IS_FATAL ANY)

set(_missing_component_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_missing_component_build_dir}"
    ${_nested_generator_args}
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
        "find_package(ptx_frontend COMPONENTS definitely_unknown) unexpectedly succeeded")
endif()
string(CONCAT _missing_component_output
       "${_missing_component_stdout}" "${_missing_component_stderr}")
if(NOT _missing_component_output MATCHES
       "Unsupported ptx_frontend component\\(s\\): definitely_unknown")
    message(FATAL_ERROR
        "Missing-component configure failed without the expected diagnostic:\n"
        "${_missing_component_output}")
endif()

set(_removed_codegen_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_removed_codegen_build_dir}"
    ${_nested_generator_args}
    ${_consumer_common_args}
    -DPTX_FRONTEND_TEST_REMOVED_CODEGEN_COMPONENT=ON
)
execute_process(
    COMMAND ${_removed_codegen_command}
    RESULT_VARIABLE _removed_codegen_result
    OUTPUT_VARIABLE _removed_codegen_stdout
    ERROR_VARIABLE _removed_codegen_stderr
)
if(_removed_codegen_result EQUAL 0)
    message(FATAL_ERROR
        "find_package(ptx_frontend COMPONENTS codegen) unexpectedly succeeded")
endif()
string(CONCAT _removed_codegen_output
       "${_removed_codegen_stdout}" "${_removed_codegen_stderr}")
if(NOT _removed_codegen_output MATCHES
       "Unsupported ptx_frontend component\\(s\\): codegen")
    message(FATAL_ERROR
        "Removed-codegen configure failed without the expected diagnostic:\n"
        "${_removed_codegen_output}")
endif()

file(COPY "${_install_dir}/" DESTINATION "${_missing_spec_install_dir}")
file(REMOVE_RECURSE
    "${_missing_spec_install_dir}/share/ptx_frontend/ptx_spec")
set(_missing_spec_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_missing_spec_build_dir}"
    ${_nested_generator_args}
    "-DCMAKE_PREFIX_PATH=${_missing_spec_install_dir}"
    ${_consumer_dependency_args}
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
