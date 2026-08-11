find_package(GTest CONFIG REQUIRED)

# target test_lexer
add_executable(test_lexer)
file(GLOB _test_lexer_src_files lexer/*.cpp)
target_sources(
    test_lexer
    PRIVATE ${_test_lexer_src_files}
)
target_link_libraries(
    test_lexer
    PRIVATE
    ptx_frontend
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_lexer)

# target test_syntax
add_executable(test_syntax)
file(GLOB _test_syntax_src_files syntax/*.cpp)
target_sources(
    test_syntax
    PRIVATE ${_test_syntax_src_files}
)
target_link_libraries(
    test_syntax
    PRIVATE
    ptx_frontend
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_syntax)

# target codegen_test
add_test(
    NAME codegen_test
    COMMAND
    ${Python3_EXECUTABLE}
    -m unittest discover
    -s ${PROJECT_SOURCE_DIR}/python/tests
    -t ${PROJECT_SOURCE_DIR}/python
    -p test_*.py
)
set_tests_properties(
    codegen_test
    PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)