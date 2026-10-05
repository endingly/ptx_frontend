cmake_minimum_required(VERSION 3.24)
project(resolved_ir_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ptx_frontend 0.7.0 CONFIG REQUIRED
    COMPONENTS resolved_ir ptx_spec)

foreach(resource IN ITEMS PTX_SPEC_SCHEMA PTX_CPP_BACKEND_SPEC PTX_CPP_BACKEND_SCHEMA)
    if(NOT EXISTS "${ptx_frontend_${resource}}")
        message(FATAL_ERROR "Missing installed ptx_frontend resource: ${resource}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${ptx_frontend_PTX_SPEC_DIR}")
    message(FATAL_ERROR "Missing installed PTX specification directory")
endif()

add_executable(resolved_ir_consumer main.cpp)
target_compile_features(resolved_ir_consumer PRIVATE cxx_std_23)
target_link_libraries(resolved_ir_consumer PRIVATE
    ptx_frontend::resolved_ir)
