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
file(GLOB_RECURSE PTX_SPEC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/*.yaml)
file(GLOB_RECURSE PTX_BACKEND_FILES ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/*.yaml)
file(GLOB_RECURSE PYTHON_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/python/*.py)
file(GLOB_RECURSE PYTHON_TEMPLATES ${CMAKE_CURRENT_SOURCE_DIR}/python/templates/*.j2)
set(PTX_GENERATED_FILES
    ${PTX_GEN_PUBLIC_DIR}/ptx_ir_integer_arithmetic.gen.hpp
    ${PTX_GEN_PUBLIC_DIR}/ptx_ir_registry.gen.hpp
    ${PTX_GEN_PRIVATE_DIR}/ptx_parser_util.gen.hpp
    ${PTX_GEN_PRIVATE_DIR}/ptx_parser_integer_arithmetic.gen.hpp
    ${PTX_GEN_PRIVATE_DIR}/ptx_parser_integer_arithmetic.gen.cpp
    ${PTX_GEN_PRIVATE_DIR}/ptx_parser_registry.gen.hpp
    ${PTX_GEN_PRIVATE_DIR}/ptx_parser_registry.gen.cpp
)
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
file(GLOB_RECURSE ptx_frontend_generated_srcs ${PTX_GEN_PUBLIC_DIR}/*.gen.cpp ${PTX_GEN_PRIVATE_DIR}/*.gen.cpp)
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
