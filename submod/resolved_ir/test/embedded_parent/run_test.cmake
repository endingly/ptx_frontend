if(NOT DEFINED PTX_FRONTEND_SOURCE_DIR OR NOT DEFINED PTX_BINARY_DIR)
    message(FATAL_ERROR "PTX_FRONTEND_SOURCE_DIR and PTX_BINARY_DIR are required")
endif()

set(_test_root "${PTX_BINARY_DIR}/test/embedded_parent")
set(_build_dir "${_test_root}/build")
file(REMOVE_RECURSE "${_test_root}")

set(_generator_args)
if(DEFINED PTX_CMAKE_GENERATOR AND NOT PTX_CMAKE_GENERATOR STREQUAL "")
    list(APPEND _generator_args -G "${PTX_CMAKE_GENERATOR}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_PLATFORM AND
   NOT PTX_CMAKE_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _generator_args -A "${PTX_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED PTX_CMAKE_GENERATOR_TOOLSET AND
   NOT PTX_CMAKE_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _generator_args -T "${PTX_CMAKE_GENERATOR_TOOLSET}")
endif()

set(_cache_args -DBUILD_TESTING=OFF -DPTX_USE_CCACHE=OFF)
if(DEFINED PTX_FMT_DIR AND NOT PTX_FMT_DIR STREQUAL "")
    list(APPEND _cache_args "-Dfmt_DIR=${PTX_FMT_DIR}")
endif()
if(DEFINED PTX_MAGIC_ENUM_DIR AND NOT PTX_MAGIC_ENUM_DIR STREQUAL "")
    list(APPEND _cache_args "-Dmagic_enum_DIR=${PTX_MAGIC_ENUM_DIR}")
endif()
foreach(_cache_spec IN ITEMS
        "CMAKE_MAKE_PROGRAM:FILEPATH"
        "CMAKE_BUILD_TYPE:STRING"
        "CMAKE_TOOLCHAIN_FILE:FILEPATH"
        "CMAKE_C_COMPILER:FILEPATH"
        "CMAKE_CXX_COMPILER:FILEPATH"
        "Python3_EXECUTABLE:FILEPATH")
    string(REPLACE ":" ";" _cache_spec_parts "${_cache_spec}")
    list(GET _cache_spec_parts 0 _cache_var)
    list(GET _cache_spec_parts 1 _cache_type)
    set(_forward_var "PTX_${_cache_var}")
    if(_cache_var STREQUAL "Python3_EXECUTABLE")
        set(_forward_var "PTX_PYTHON_EXECUTABLE")
    endif()
    if(DEFINED ${_forward_var} AND NOT "${${_forward_var}}" STREQUAL "")
        list(APPEND _cache_args
            "-D${_cache_var}:${_cache_type}=${${_forward_var}}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${PTX_FRONTEND_SOURCE_DIR}/submod/resolved_ir/test/embedded_parent"
        -B "${_build_dir}"
        ${_generator_args}
        "-DPTX_FRONTEND_SOURCE_DIR=${PTX_FRONTEND_SOURCE_DIR}"
        ${_cache_args}
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --parallel
    COMMAND_ERROR_IS_FATAL ANY
)
