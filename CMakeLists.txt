cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(CCACHE_FOUND "Use ccache to speed up recompilation" ON)
option(BUILD_TESTING "enable test program building" OFF)

if(CCACHE_FOUND)
    find_program(CCACHE_FOUND ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    include(GoogleTest)
endif()

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
file(GLOB_RECURSE PYTHON_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/python/*.py)
file(GLOB_RECURSE PYTHON_TEMPLATES ${CMAKE_CURRENT_SOURCE_DIR}/python/templates/*.j2)
add_custom_command(
    OUTPUT ${PTX_GEN_DIR}/ptx_ir_instr.gen.hpp
    COMMAND 
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/code_gen/gen_ir.py
        --spec ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml
        --backend ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/integer_arith.yaml
        --template-dir ${CMAKE_CURRENT_SOURCE_DIR}/python/templates
        --output ${PTX_GEN_DIR}/ptx_ir_instr.gen.hpp
    DEPENDS 
        ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml
        ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/integer_arith.yaml 
        ${PYTHON_SCRIPTS}
        ${PYTHON_TEMPLATES}
    COMMENT "Generating PTX IR struct code from ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml"
)
add_custom_command(
    OUTPUT ${PTX_GEN_DIR}/ptx_parser.gen.hpp
    COMMAND 
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/code_gen/gen_parser.py
        --spec ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml
        --backend ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/integer_arith.yaml
        --template-dir ${CMAKE_CURRENT_SOURCE_DIR}/python/templates
        --output ${PTX_GEN_DIR}/ptx_parser.gen.hpp
    DEPENDS 
        ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml
        ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/integer_arith.yaml 
        ${PYTHON_SCRIPTS}
        ${PYTHON_TEMPLATES}
    COMMENT "Generating PTX parser code from ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/integer_arith.yaml"
)
add_custom_target(
    auto_gen_code
    DEPENDS
        ${PTX_GEN_DIR}/ptx_ir_instr.gen.hpp
        ${PTX_GEN_DIR}/ptx_parser.gen.hpp
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

add_dependencies(ptx_frontend auto_gen_code)

# # target ptx_frontend_test
if(BUILD_TESTING)
    aux_source_directory(test ptx_frontend_test_files)
    add_executable(ptx_frontend_test ${ptx_frontend_test_files})
    target_link_libraries(ptx_frontend_test PRIVATE ptx_frontend)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(ptx_frontend_test PRIVATE GTest::gtest GTest::gtest_main)
    gtest_discover_tests(ptx_frontend_test)
endif()