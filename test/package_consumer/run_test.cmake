if(NOT DEFINED PTX_SOURCE_DIR OR NOT DEFINED PTX_BINARY_DIR)
    message(FATAL_ERROR "PTX_SOURCE_DIR and PTX_BINARY_DIR are required")
endif()

set(_install_dir "${PTX_BINARY_DIR}/test/package_consumer/install")
set(_build_dir "${PTX_BINARY_DIR}/test/package_consumer/build")
file(REMOVE_RECURSE "${_install_dir}" "${_build_dir}")

set(_install_command
    "${CMAKE_COMMAND}" --install "${PTX_BINARY_DIR}" --prefix "${_install_dir}"
)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _install_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_install_command} COMMAND_ERROR_IS_FATAL ANY)

set(_resolved_ir_header
    "${_install_dir}/include/ptx_ir/resolved/resolved_ir.gen.hpp")
if(NOT EXISTS "${_resolved_ir_header}")
    message(FATAL_ERROR
        "Generated public header was not installed at the expected path: "
        "${_resolved_ir_header}")
endif()

set(_legacy_resolved_ir_header
    "${_install_dir}/include/resolved_ir.gen.hpp")
if(EXISTS "${_legacy_resolved_ir_header}")
    message(FATAL_ERROR
        "Generated public header was installed at the retired flat path: "
        "${_legacy_resolved_ir_header}")
endif()

set(_configure_command
    "${CMAKE_COMMAND}"
    -S "${PTX_SOURCE_DIR}/test/package_consumer"
    -B "${_build_dir}"
    "-DCMAKE_PREFIX_PATH=${_install_dir}"
)
if(DEFINED PTX_TOOLCHAIN_FILE AND NOT PTX_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _configure_command
         "-DCMAKE_TOOLCHAIN_FILE=${PTX_TOOLCHAIN_FILE}")
endif()
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _configure_command "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _configure_command
         "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
execute_process(COMMAND ${_configure_command} COMMAND_ERROR_IS_FATAL ANY)

set(_build_command "${CMAKE_COMMAND}" --build "${_build_dir}")
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_build_command} COMMAND_ERROR_IS_FATAL ANY)

set(_test_command "${CMAKE_CTEST_COMMAND}" --test-dir "${_build_dir}"
                  --output-on-failure)
if(DEFINED PTX_TEST_CONFIG AND NOT PTX_TEST_CONFIG STREQUAL "")
    list(APPEND _test_command --build-config "${PTX_TEST_CONFIG}")
endif()
execute_process(COMMAND ${_test_command} COMMAND_ERROR_IS_FATAL ANY)
