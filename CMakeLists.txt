cmake_minimum_required(VERSION 3.28.0)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
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

# set public/private include directories
include(GNUInstallDirs)
set(PTX_GEN_PUBLIC_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/public)
set(PTX_GEN_PRIVATE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/private)

# Flex target
include(FindFLEX)
set(PTX_LEXER_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/ptx_lexer.l)
file(MAKE_DIRECTORY ${PTX_GEN_PRIVATE_DIR})
set(PTX_LEXER_OUT ${PTX_GEN_PRIVATE_DIR}/_ptx_lexer.cpp)
set(PTX_LEXER_HDR ${PTX_GEN_PRIVATE_DIR}/_ptx_lexer.hpp)
FLEX_TARGET(
    ptx_lexer
    ${PTX_LEXER_IN}
    ${PTX_LEXER_OUT}
    COMPILE_FLAGS "--header-file=${PTX_LEXER_HDR} -w"
)

# yaml generator target
# find python
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE PTX_SPEC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/*.yaml)
file(GLOB_RECURSE PTX_BACKEND_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/*.yaml)
file(GLOB_RECURSE PYTHON_SCRIPTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/python/*.py)
file(GLOB_RECURSE PYTHON_TEMPLATES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/python/templates/*.j2)

execute_process(
    COMMAND
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_all.py
    --spec-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec
    --backend-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec
    --template-dir ${CMAKE_CURRENT_SOURCE_DIR}/python/templates
    --output ${CMAKE_CURRENT_BINARY_DIR}/generated
    --list-outputs
    RESULT_VARIABLE PTX_GENERATED_FILES_RESULT
    OUTPUT_VARIABLE PTX_GENERATED_FILES_OUTPUT
    ERROR_VARIABLE PTX_GENERATED_FILES_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT PTX_GENERATED_FILES_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to determine generated files: ${PTX_GENERATED_FILES_ERROR}")
endif()

string(REPLACE "\n" ";" PTX_GENERATED_FILES "${PTX_GENERATED_FILES_OUTPUT}")
add_custom_command(
    OUTPUT ${PTX_GENERATED_FILES}
    COMMAND
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_all.py
    --spec-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec
    --backend-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec
    --template-dir ${CMAKE_CURRENT_SOURCE_DIR}/python/templates
    --output ${CMAKE_CURRENT_BINARY_DIR}/generated
    DEPENDS
    ${PTX_SPEC_FILES}
    ${PTX_BACKEND_FILES}
    ${PYTHON_SCRIPTS}
    ${PYTHON_TEMPLATES}
    COMMENT "Generating PTX frontend code"
)
add_custom_target(
    auto_gen_code
    DEPENDS
    ${PTX_GENERATED_FILES}
)

# target ptx_frontend
file(GLOB_RECURSE ptx_frontend_srcs src/*.cpp)
set(ptx_frontend_generated_srcs ${PTX_GENERATED_FILES})
list(FILTER ptx_frontend_generated_srcs INCLUDE REGEX "\\.gen\\.cpp$")
add_library(ptx_frontend ${ptx_frontend_srcs} ${FLEX_ptx_lexer_OUTPUTS} ${ptx_frontend_generated_srcs})
target_include_directories(
    ptx_frontend
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PTX_GEN_PUBLIC_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE

    # ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PTX_GEN_PRIVATE_DIR}
)
target_link_libraries(ptx_frontend PRIVATE ${FLEX_LIBRARIES})
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS dynamic_bitset)
find_package(frozen CONFIG REQUIRED)
target_link_libraries(
    ptx_frontend 
    PUBLIC fmt::fmt magic_enum::magic_enum Boost::dynamic_bitset frozen::frozen
)

add_dependencies(ptx_frontend auto_gen_code)

# Test targets
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(ptx_lexer_test test/test_ptx_lexer.cpp)
    target_link_libraries(
        ptx_lexer_test
        PRIVATE
        ptx_frontend
        GTest::gtest
        GTest::gtest_main
    )
    gtest_discover_tests(ptx_lexer_test)

    add_executable(
        test_ptx_synatax_parser
        test/test_ptx_syntax_ast_parser.cpp
    )
    target_link_libraries(
        test_ptx_synatax_parser
        PRIVATE
        ptx_frontend
        GTest::gtest
        GTest::gtest_main
    )
    target_compile_options(
        test_ptx_synatax_parser PRIVATE -gdwarf-4
    )
    gtest_discover_tests(test_ptx_synatax_parser)

    # add_executable(
    #     ptx_parser_test
    #     test/test_ptx_parser.cpp
    #     test/test_ptx_parser_lifetime.cpp
    # )
    # target_include_directories(
    #     ptx_parser_test
    #     PRIVATE
    #     ${PTX_GEN_PRIVATE_DIR}
    # )
    # target_link_libraries(
    #     ptx_parser_test
    #     PRIVATE
    #     ptx_frontend
    #     GTest::gtest
    #     GTest::gtest_main
    # )
    # gtest_discover_tests(ptx_parser_test)

    # add_test(
    #     NAME ptx_codegen_test
    #     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/python/test_codegen.py
    # )
    # set_tests_properties(
    #     ptx_codegen_test
    #     PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    # )
endif()

# ################################################################
# install targets
# ################################################################
include(install_project_targets)

install_project_targets(
    PROJECT ptx_frontend
    VERSION ${PROJECT_VERSION}
    TARGETS ptx_frontend
    EXPORT ptx_frontendTargets
    INCLUDE_DIRS 
        ${CMAKE_SOURCE_DIR}/include
        ${PTX_GEN_PUBLIC_DIR}
)
