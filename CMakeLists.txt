cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

aux_source_directory(src ptx_frontend_srcs)
add_library(ptx_frontend ${ptx_frontend_srcs} ${FLEX_ptx_lexer_OUTPUTS})
target_include_directories(ptx_frontend PUBLIC include ${PTX_GEN_DIR})
target_link_libraries(ptx_frontend PRIVATE ${FLEX_LIBRARIES})

# # target ptx_frontend_test
aux_source_directory(test ptx_frontend_test_files)
add_executable(ptx_frontend_test ${ptx_frontend_test_files})
target_link_libraries(ptx_frontend_test PRIVATE ptx_frontend)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(ptx_frontend_test PRIVATE GTest::gtest GTest::gtest_main)