cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Flex target
include(FindFLEX)
set(PTX_LEXER_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/ptx_lexer.l)
set(PTX_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PTX_GEN_DIR})
set(PTX_LEXER_OUT ${PTX_GEN_DIR}/_ptx_lexer.cpp)
set(PTX_LEXER_HDR ${PTX_GEN_DIR}/_ptx_lexer.hpp)
FLEX_TARGET(
    ptx_lexer
    ${PTX_LEXER_IN}
    ${PTX_LEXER_OUT}
    COMPILE_FLAGS "--header-file=${PTX_LEXER_HDR} -w"
)

# yaml generator target
# find python
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(from_yaml ${CMAKE_CURRENT_SOURCE_DIR}/instructions/integer_arith.yaml)
add_custom_command(
    OUTPUT ${PTX_GEN_DIR}/type_checker.src.gen 
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_type_checker_code.py
    --input ${from_yaml}
    --output ${PTX_GEN_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_type_checker_code.py ${from_yaml}
    COMMENT "Generating type checker code from ${from_yaml}"
)
add_custom_target(
    gen_type_checker_code
    DEPENDS ${PTX_GEN_DIR}/type_checker.src.gen
)

# target ptx_frontend
file(GLOB_RECURSE ptx_frontend_srcs src/*.cpp)
add_library(ptx_frontend ${ptx_frontend_srcs} ${FLEX_ptx_lexer_OUTPUTS})
target_include_directories(
    ptx_frontend
    PUBLIC include ${PTX_GEN_DIR}
)
target_link_libraries(ptx_frontend PRIVATE ${FLEX_LIBRARIES})
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(ptx_frontend PUBLIC fmt::fmt magic_enum::magic_enum)

add_dependencies(ptx_frontend gen_type_checker_code)

# # target ptx_frontend_test
aux_source_directory(test ptx_frontend_test_files)
add_executable(ptx_frontend_test ${ptx_frontend_test_files})
target_link_libraries(ptx_frontend_test PRIVATE ptx_frontend)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(ptx_frontend_test PRIVATE GTest::gtest GTest::gtest_main)