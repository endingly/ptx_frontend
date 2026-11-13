# Private implementation instrumentation: never propagate flags to SDK consumers.
if(PTX_FRONTEND_BUILD_FUZZERS)
    if(BUILD_TESTING)
        message(FATAL_ERROR "PTX_FRONTEND_BUILD_FUZZERS requires BUILD_TESTING=OFF")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PTX_FRONTEND_BUILD_FUZZERS requires Clang/libFuzzer")
    endif()
    foreach(component IN ITEMS base lexer cst syntax binding semantic resolved_ir)
        # common is header-only; its inline code is instrumented in these consumers.
        target_compile_options(ptx_frontend_${component} PRIVATE
            -fsanitize=fuzzer-no-link,address,undefined
            -fno-sanitize-recover=all -fno-omit-frame-pointer)
    endforeach()
    add_executable(fuzz_lexer_cst
        "${PROJECT_SOURCE_DIR}/submod/cst/fuzz/lexer_cst_fuzz.cpp")
    target_link_libraries(fuzz_lexer_cst PRIVATE ptx_frontend::cst)
    add_executable(fuzz_frontend
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_fuzz.cpp"
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_fuzz_entry.cpp")
    target_link_libraries(fuzz_frontend PRIVATE ptx_frontend::resolved_ir)
    add_executable(fuzz_frontend_canary
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_fuzz.cpp"
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_canary.cpp")
    target_link_libraries(fuzz_frontend_canary PRIVATE ptx_frontend::resolved_ir)
    foreach(target IN ITEMS fuzz_lexer_cst fuzz_frontend fuzz_frontend_canary)
        target_compile_options(${target} PRIVATE
            -fsanitize=fuzzer-no-link,address,undefined
            -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all)
    endforeach()
    foreach(target IN ITEMS fuzz_lexer_cst fuzz_frontend)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    endforeach()
elseif(BUILD_TESTING)
    # The same non-public canary also runs in ordinary compiler builds.
    add_executable(fuzz_frontend_canary
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_fuzz.cpp"
        "${PROJECT_SOURCE_DIR}/fuzz/frontend_canary.cpp")
    target_link_libraries(fuzz_frontend_canary PRIVATE ptx_frontend::resolved_ir)
    add_test(NAME fuzz_frontend_canary COMMAND fuzz_frontend_canary)
    set_tests_properties(fuzz_frontend_canary PROPERTIES TIMEOUT 30)
endif()
