include("${CMAKE_CURRENT_LIST_DIR}/../ptx93_postfix/focused-hook.cmake")

function(ptx93_focused_st_tests)
    find_package(GTest CONFIG REQUIRED)
    add_executable(ptx93_st_tests
        "${PROJECT_SOURCE_DIR}/submod/resolved_ir/test/test_st_immediate.cpp"
        "${PROJECT_SOURCE_DIR}/submod/resolved_ir/test/test_st_completeness.cpp"
        "${PROJECT_SOURCE_DIR}/submod/resolved_ir/test/test_module_snapshot.cpp")
    target_link_libraries(ptx93_st_tests PRIVATE
        GTest::gtest_main ptx_frontend::resolved_ir)
endfunction()

cmake_language(DEFER CALL ptx93_focused_st_tests)
