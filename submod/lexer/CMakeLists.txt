file(GLOB_RECURSE lexer_srcs src/*cpp)
add_library(lexer)
target_sources(lexer PRIVATE ${lexer_srcs})
add_library(ptx_frontend::lexer ALIAS lexer)
register_headers(
    lexer
    lexer
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    lexer
    PUBLIC ptx_frontend::common
)

## flex target
include(FindFLEX)
set(PTX_LEXER_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/ptx_lexer.l)
set(PTX_LEXER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PTX_LEXER_OUT_DIR})
set(PTX_LEXER_OUT ${PTX_LEXER_OUT_DIR}/_ptx_lexer.cpp)
set(PTX_LEXER_HDR ${PTX_LEXER_OUT_DIR}/_ptx_lexer.hpp)
FLEX_TARGET(
    flex_lexer_target
    ${PTX_LEXER_IN}
    ${PTX_LEXER_OUT}
    COMPILE_FLAGS "--header-file=${PTX_LEXER_HDR} -w"
)

target_include_directories(
    lexer PRIVATE ${PTX_LEXER_OUT_DIR}
)
target_sources(
    lexer PRIVATE ${PTX_LEXER_OUT}
)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_lexer_srcs test/*.cpp)
    add_executable(test_lexer ${test_lexer_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_lexer PRIVATE GTest::gtest GTest::gtest_main lexer)
    gtest_discover_tests(test_lexer)
endif()
