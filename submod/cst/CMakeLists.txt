file(GLOB_RECURSE cst_srcs src/*cpp)
add_library(ptx_frontend_cst STATIC ${cst_srcs})
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
    target_sources(test_cst PRIVATE fuzz/lexer_cst_fuzz.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_cst PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::cst)
    # Each parser/fuzz regression runs in its own time-bounded CTest process.
    gtest_discover_tests(test_cst PROPERTIES TIMEOUT 10)
endif()
