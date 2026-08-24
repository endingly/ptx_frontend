cmake_minimum_required(VERSION 3.28.0)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(PTX_USE_CCACHE "Use ccache to speed up compilation" ON)
option(BUILD_TESTING "enable test program building" OFF)

if(PTX_USE_CCACHE)
    find_program(CCACHE_EXECUTABLE ccache)
    if(CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
    else()
        message(WARNING "PTX_USE_CCACHE is ON, but ccache was not found")
    endif()
endif()

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    include(GoogleTest)
endif()

# # set public/private include directories
# include(GNUInstallDirs)
# set(PTX_GEN_PUBLIC_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/public)
# set(PTX_GEN_PRIVATE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/private)

# # Flex target
# include(FindFLEX)
# set(PTX_LEXER_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/ptx_lexer.l)
# file(MAKE_DIRECTORY ${PTX_GEN_PRIVATE_DIR})
# set(PTX_LEXER_OUT ${PTX_GEN_PRIVATE_DIR}/_ptx_lexer.cpp)
# set(PTX_LEXER_HDR ${PTX_GEN_PRIVATE_DIR}/_ptx_lexer.hpp)
# FLEX_TARGET(
#     ptx_lexer
#     ${PTX_LEXER_IN}
#     ${PTX_LEXER_OUT}
#     COMPILE_FLAGS "--header-file=${PTX_LEXER_HDR} -w"
# )

# # yaml generator target
# # find python
# find_package(Python3 COMPONENTS Interpreter REQUIRED)
# file(GLOB_RECURSE PTX_SPEC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec/*.yaml)
# # The generator's output list contains one resolved-IR source per
# # ``codegen_category``.  A category change does not alter the GLOB result, so
# # list every discovered spec as a configure dependency as well: CMake must
# # rerun ``--list-outputs`` before choosing the library's generated sources.
# set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PTX_SPEC_FILES})
# set(PTX_INSTRUCTION_SCHEMA
#     ${CMAKE_CURRENT_SOURCE_DIR}/instructions/schemas/ptx-instr-v1.schema.yaml
# )
# set(PTX_CPP_BACKEND_SPEC
#     ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec/ptx_frontend.yaml
# )
# set(PTX_CPP_BACKEND_SCHEMA
#     ${CMAKE_CURRENT_SOURCE_DIR}/instructions/schemas/ptx-cpp-backend-v1.schema.yaml
# )
# file(GLOB_RECURSE PYTHON_CODEGEN_MODULES CONFIGURE_DEPENDS
#     ${CMAKE_CURRENT_SOURCE_DIR}/python/base/*.py
#     ${CMAKE_CURRENT_SOURCE_DIR}/python/code_gen/*.py
#     ${CMAKE_CURRENT_SOURCE_DIR}/python/ir/*.py
# )
# set(PYTHON_CODEGEN_SCRIPTS
#     ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_all.py
# )
# # Generator changes can alter the declared output topology (for example by
# # adding a new generated translation unit). Reconfigure before consuming the
# # output list so such files are also attached to the library target.
# set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
#     ${PYTHON_CODEGEN_MODULES}
#     ${PYTHON_CODEGEN_SCRIPTS}
# )

# execute_process(
#     COMMAND
#     ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_all.py
#     --spec-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec
#     --backend-spec ${PTX_CPP_BACKEND_SPEC}
#     --output ${CMAKE_CURRENT_BINARY_DIR}/generated
#     --list-outputs
#     RESULT_VARIABLE PTX_GENERATED_FILES_RESULT
#     OUTPUT_VARIABLE PTX_GENERATED_FILES_OUTPUT
#     ERROR_VARIABLE PTX_GENERATED_FILES_ERROR
#     OUTPUT_STRIP_TRAILING_WHITESPACE
# )

# if(NOT PTX_GENERATED_FILES_RESULT EQUAL 0)
#     message(FATAL_ERROR "Failed to determine generated files: ${PTX_GENERATED_FILES_ERROR}")
# endif()

# string(REPLACE "\n" ";" PTX_GENERATED_FILES "${PTX_GENERATED_FILES_OUTPUT}")
# add_custom_command(
#     OUTPUT ${PTX_GENERATED_FILES}
#     COMMAND
#     ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/scripts/gen_all.py
#     --spec-dir ${CMAKE_CURRENT_SOURCE_DIR}/instructions/ptx_spec
#     --backend-spec ${PTX_CPP_BACKEND_SPEC}
#     --output ${CMAKE_CURRENT_BINARY_DIR}/generated
#     DEPENDS
#     ${PTX_SPEC_FILES}
#     ${PTX_INSTRUCTION_SCHEMA}
#     ${PTX_CPP_BACKEND_SPEC}
#     ${PTX_CPP_BACKEND_SCHEMA}
#     ${PYTHON_CODEGEN_MODULES}
#     ${PYTHON_CODEGEN_SCRIPTS}
#     COMMENT "Generating PTX frontend code"
# )
# add_custom_target(
#     auto_gen_code
#     DEPENDS
#     ${PTX_GENERATED_FILES}
# )

# # target ptx_frontend
# file(GLOB_RECURSE ptx_frontend_srcs src/*.cpp)
# set(ptx_frontend_generated_srcs ${PTX_GENERATED_FILES})
# list(FILTER ptx_frontend_generated_srcs INCLUDE REGEX "\\.gen\\.cpp$")
# add_library(ptx_frontend ${ptx_frontend_srcs} ${FLEX_ptx_lexer_OUTPUTS} ${ptx_frontend_generated_srcs})
# target_include_directories(
#     ptx_frontend
#     PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#     $<BUILD_INTERFACE:${PTX_GEN_PUBLIC_DIR}>
#     $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
#     PRIVATE

#     # ${CMAKE_CURRENT_SOURCE_DIR}/src
#     ${PTX_GEN_PRIVATE_DIR}
# )
# find_package(fmt CONFIG REQUIRED)
# find_package(magic_enum CONFIG REQUIRED)
# target_link_libraries(
#     ptx_frontend 
#     PUBLIC fmt::fmt magic_enum::magic_enum
# )

# add_dependencies(ptx_frontend auto_gen_code)

# # Test targets
# if(BUILD_TESTING)
#     add_subdirectory(test)
# endif()

add_subdirectory(submod)

# ################################################################
# install targets
# ################################################################
# include(install_project_targets)

# install_project_targets(
#     PROJECT ptx_frontend
#     VERSION ${PROJECT_VERSION}
#     TARGETS ptx_frontend
#     EXPORT ptx_frontendTargets
#     INCLUDE_DIRS 
#         ${CMAKE_SOURCE_DIR}/include
#     DEPENDENCIES
#         "fmt CONFIG"
#         "magic_enum CONFIG"
# )

# install(
#     FILES ${PTX_GEN_PUBLIC_DIR}/resolved_ir.gen.hpp
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ptx_ir/resolved
# )
