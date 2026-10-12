file(GLOB_RECURSE cst_srcs src/*cpp)
add_library(ptx_frontend_cst ${cst_srcs})
set_property(TARGET ptx_frontend_cst PROPERTY EXPORT_NAME cst)
add_library(ptx_frontend::cst ALIAS ptx_frontend_cst)
register_headers(
    ptx_frontend_cst
    cst
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    ptx_frontend_cst
    PUBLIC
    ptx_frontend::lexer
)

option(PTX_FRONTEND_USE_BISON_PARSER "Use the experimental Bison constant-expression parser" OFF)
if(PTX_FRONTEND_USE_BISON_PARSER)
    find_package(BISON 3.8 REQUIRED)
    set(cst_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${cst_generated_dir}")
    BISON_TARGET(ptx_constant_parser src/ptx_constant_parser.y
        "${cst_generated_dir}/ptx_constant_parser.cpp"
        DEFINES_FILE "${cst_generated_dir}/ptx_constant_parser.hpp"
        COMPILE_FLAGS "--warnings=error")
    BISON_TARGET(ptx_generated_parser src/ptx_parser.y
        "${cst_generated_dir}/ptx_parser.cpp"
        DEFINES_FILE "${cst_generated_dir}/ptx_parser.hpp"
        COMPILE_FLAGS "--warnings=error")
    target_sources(ptx_frontend_cst PRIVATE ${BISON_ptx_constant_parser_OUTPUTS}
        bison/ptx_constant_driver.cpp ${BISON_ptx_generated_parser_OUTPUTS}
        bison/ptx_parser_driver.cpp)
    target_include_directories(ptx_frontend_cst PRIVATE
        "${cst_generated_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/bison")
    target_compile_definitions(ptx_frontend_cst PRIVATE PTX_FRONTEND_USE_BISON_PARSER=1)
endif()

option(PTX_FRONTEND_BUILD_FUZZERS "Build the Clang lexer/CST fuzz target" OFF)
if(PTX_FRONTEND_BUILD_FUZZERS)
    if(BUILD_TESTING)
        message(FATAL_ERROR
            "PTX_FRONTEND_BUILD_FUZZERS requires a separate build with BUILD_TESTING=OFF")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PTX_FRONTEND_BUILD_FUZZERS requires Clang/libFuzzer")
    endif()
    target_compile_options(ptx_frontend_lexer PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_options(ptx_frontend_cst PRIVATE -fsanitize=fuzzer-no-link)
    add_executable(fuzz_lexer_cst fuzz/lexer_cst_fuzz.cpp)
    target_link_libraries(fuzz_lexer_cst PRIVATE ptx_frontend::cst)
    target_compile_options(fuzz_lexer_cst PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_lexer_cst PRIVATE -fsanitize=fuzzer)
endif()

if(BUILD_TESTING)
    file(GLOB_RECURSE test_cst_srcs test/*.cpp)
    add_executable(test_cst ${test_cst_srcs})
    if(PTX_FRONTEND_USE_BISON_PARSER)
        target_sources(test_cst PRIVATE bison/test_ptx_generated_parser.cpp)
        target_include_directories(test_cst PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/bison")
    endif()
    target_sources(test_cst PRIVATE fuzz/lexer_cst_fuzz.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_cst PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::cst)
    # Each parser/fuzz regression runs in its own time-bounded CTest process.
    gtest_discover_tests(test_cst PROPERTIES TIMEOUT 10)
endif()
