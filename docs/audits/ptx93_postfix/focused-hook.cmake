# Reproduce focused MOV checks alongside the existing audit executables.
include("${CMAKE_CURRENT_LIST_DIR}/../ptx93_probes/probe-hook.cmake")

function(ptx93_focused_vector_tests)
    find_package(GTest CONFIG REQUIRED)
    add_executable(ptx93_vector_tests
        "${PROJECT_SOURCE_DIR}/submod/resolved_ir/test/test_vector_mov.cpp"
        "${PROJECT_SOURCE_DIR}/submod/resolved_ir/test/test_mov_completeness.cpp")
    target_link_libraries(ptx93_vector_tests PRIVATE
        GTest::gtest_main ptx_frontend::resolved_ir)
endfunction()

cmake_language(DEFER CALL ptx93_focused_vector_tests)
