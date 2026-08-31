cmake_minimum_required(VERSION 3.28)
project(ptx_frontend_package_consumer LANGUAGES CXX)

if(PTX_FRONTEND_TEST_UNKNOWN_COMPONENT)
    find_package(
        ptx_frontend 0.0.1 CONFIG REQUIRED
        COMPONENTS definitely_unknown
    )
    return()
endif()

find_package(
    ptx_frontend 0.0.1 CONFIG REQUIRED
    COMPONENTS
        ptx_frontend
        common
        base
        lexer
        cst
        syntax
        binding
        semantic
        resolved_ir
        ptx_spec
        codegen
)
enable_testing()

foreach(_component IN ITEMS
        ptx_frontend
        common
        base
        lexer
        cst
        syntax
        binding
        semantic
        resolved_ir)
    if(NOT TARGET ptx_frontend::${_component})
        message(FATAL_ERROR
            "Missing imported target: ptx_frontend::${_component}")
    endif()
endforeach()

if(NOT ptx_frontend_ptx_spec_FOUND)
    message(FATAL_ERROR "ptx_spec component was not found")
endif()
if(NOT ptx_frontend_codegen_FOUND)
    message(FATAL_ERROR "codegen component was not found")
endif()
if(NOT EXISTS "${ptx_frontend_PTX_SPEC_DIR}/arithmetic.yaml")
    message(FATAL_ERROR "Installed PTX spec is incomplete")
endif()
if(NOT EXISTS "${ptx_frontend_PTX_SPEC_SCHEMA}")
    message(FATAL_ERROR "Installed PTX schema is missing")
endif()
if(NOT COMMAND ptx_frontend_generate)
    message(FATAL_ERROR "Installed codegen helper is missing")
endif()

set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
ptx_frontend_generate(
    SPEC_DIR "${ptx_frontend_PTX_SPEC_DIR}"
    BACKEND_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/backend.yaml"
    OUTPUT_DIR "${_generated_dir}"
)

add_executable(ptx_frontend_package_consumer main.cpp)
target_compile_features(ptx_frontend_package_consumer PRIVATE cxx_std_23)
target_link_libraries(
    ptx_frontend_package_consumer
    PRIVATE ptx_frontend::ptx_frontend
)
add_dependencies(ptx_frontend_package_consumer ptx_frontend_codegen)
add_test(NAME runs_installed_frontend COMMAND ptx_frontend_package_consumer)
