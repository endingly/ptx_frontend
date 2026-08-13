find_package(GTest CONFIG REQUIRED)

# target test_lexer
add_executable(test_lexer)
file(GLOB _test_lexer_src_files CONFIGURE_DEPENDS lexer/*.cpp)
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
file(GLOB _test_syntax_src_files CONFIGURE_DEPENDS syntax/*.cpp)
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

add_executable(test_resolve_and_check)
file(GLOB _test_resolve_and_check_src_files CONFIGURE_DEPENDS resolve_and_check/*.cpp)
target_sources(
    test_resolve_and_check
    PRIVATE ${_test_resolve_and_check_src_files}
)
target_link_libraries(
    test_resolve_and_check
    PRIVATE
    ptx_frontend
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_resolve_and_check)

add_test(
    NAME python_ir
    COMMAND
    ${Python3_EXECUTABLE}
    -m unittest discover
    -s ${PROJECT_SOURCE_DIR}/python/tests
    -t ${PROJECT_SOURCE_DIR}/python
    -p test_*.py
)
set_tests_properties(
    python_ir
    PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_test(
    NAME package_consumer
    COMMAND
    ${CMAKE_COMMAND}
    -DPTX_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DPTX_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DPTX_TEST_CONFIG=$<CONFIG>
    -DPTX_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DPTX_FMT_DIR=${fmt_DIR}
    -DPTX_MAGIC_ENUM_DIR=${magic_enum_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer/run_test.cmake
)
