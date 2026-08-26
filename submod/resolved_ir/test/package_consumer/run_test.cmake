if(NOT DEFINED PTX_SOURCE_DIR OR NOT DEFINED PTX_BINARY_DIR)
    message(FATAL_ERROR "PTX_SOURCE_DIR and PTX_BINARY_DIR are required")
endif()

set(_install_dir "${PTX_BINARY_DIR}/test/package_consumer/install")
set(_build_dir "${PTX_BINARY_DIR}/test/package_consumer/build")
set(_missing_component_build_dir
    "${PTX_BINARY_DIR}/test/package_consumer/missing_component")
file(REMOVE_RECURSE
    "${_install_dir}"
    "${_build_dir}"
    "${_missing_component_build_dir}"
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
