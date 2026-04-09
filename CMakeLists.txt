cmake_minimum_required(VERSION 4.3)
project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Flex target
include(FindFLEX)
set(PTX_LEXER_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/ptx_lexer/ptx_lexer.l)
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

add_library(ptx_frontend)
file(GLOB_RECURSE PTX_FRONTEND_CPP "src/*.cpp")
# simple cpp files
target_sources(
    ptx_frontend PRIVATE ${FLEX_ptx_lexer_OUTPUTS} ${PTX_FRONTEND_CPP}
)
file(GLOB_RECURSE PTX_FRONTEND_MPP src/*.mpp)
target_sources(
    ptx_frontend 
    PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
    FILES ${PTX_FRONTEND_MPP}
)
target_include_directories(ptx_frontend PUBLIC include ${PTX_GEN_DIR})
target_link_libraries(ptx_frontend PRIVATE ${FLEX_LIBRARIES})

# # target ptx_frontend_test
add_executable(ptx_frontend_test)
file(GLOB_RECURSE PTX_FRONTEND_TEST_MPP test/*.cpp)
target_sources(
    ptx_frontend_test PRIVATE ${PTX_FRONTEND_TEST_MPP}
)
file(GLOB_RECURSE PTX_FRONTEND_TEST_MPP test/*.mpp)
target_sources(
    ptx_frontend_test 
    PRIVATE
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/test
    FILES ${PTX_FRONTEND_TEST_MPP}
)
target_link_libraries(ptx_frontend_test PRIVATE ptx_frontend)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(ptx_frontend_test PRIVATE GTest::gtest GTest::gtest_main)