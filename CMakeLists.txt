cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/ptx_visit_dispatch.hpp
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_visit_dispatch.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/include/ptx_visit_dispatch.hpp
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_visit_dispatch.py
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ptx_ir.hpp
    COMMENT "Generating ptx_visit_dispatch.hpp"
)

add_custom_target(gen_visit_dispatch ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/ptx_visit_dispatch.hpp
)

aux_source_directory(src ptx_frontend_srcs)
add_library(ptx_frontend ${ptx_frontend_srcs})
target_include_directories(ptx_frontend PUBLIC include)

add_dependencies(ptx_frontend gen_visit_dispatch)