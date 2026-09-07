cmake_minimum_required(VERSION 3.28)
project(ptx_frontend_package_consumer LANGUAGES CXX)

if(PTX_FRONTEND_TEST_UNKNOWN_COMPONENT)
    find_package(
        ptx_frontend 0.0.1 CONFIG REQUIRED
        COMPONENTS definitely_unknown
    )
    return()
endif()

if(PTX_FRONTEND_TEST_REMOVED_CODEGEN_COMPONENT)
    find_package(
        ptx_frontend 0.0.1 CONFIG REQUIRED
        COMPONENTS codegen
    )
    return()
endif()

if(PTX_FRONTEND_TEST_PTX_SPEC_ONLY)
    find_package(
        ptx_frontend 0.0.1 CONFIG REQUIRED
        COMPONENTS ptx_spec
    )
    if(NOT ptx_frontend_ptx_spec_FOUND)
        message(FATAL_ERROR "ptx_spec component was not found")
    endif()
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
if(NOT EXISTS "${ptx_frontend_PTX_SPEC_DIR}/arithmetic.yaml")
    message(FATAL_ERROR "Installed PTX spec is incomplete")
endif()
if(NOT EXISTS "${ptx_frontend_PTX_SPEC_SCHEMA}")
    message(FATAL_ERROR "Installed PTX schema is missing")
endif()
if(COMMAND ptx_frontend_generate)
    message(FATAL_ERROR "Installed package unexpectedly exports codegen")
endif()

add_executable(ptx_frontend_package_consumer main.cpp)
target_compile_features(ptx_frontend_package_consumer PRIVATE cxx_std_23)
target_link_libraries(
    ptx_frontend_package_consumer
    PRIVATE ptx_frontend::ptx_frontend
)
add_test(NAME runs_installed_frontend COMMAND ptx_frontend_package_consumer)
