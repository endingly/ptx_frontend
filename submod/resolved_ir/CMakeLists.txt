include(generate_ptx_frontend)

file(GLOB_RECURSE resolved_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptx_frontend_resolved_ir STATIC
    ${resolved_ir_srcs}
    ${PTX_RESOLVED_IR_GENERATED_SRCS}
)
set_property(TARGET ptx_frontend_resolved_ir PROPERTY EXPORT_NAME resolved_ir)
add_library(ptx_frontend::resolved_ir ALIAS ptx_frontend_resolved_ir)
register_headers(
    ptx_frontend_resolved_ir
    resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(
    ptx_frontend_resolved_ir
    PUBLIC $<BUILD_INTERFACE:${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}>
    PRIVATE ${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}
)
find_package(fmt CONFIG REQUIRED)
target_link_libraries(
    ptx_frontend_resolved_ir
    PUBLIC ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
           ptx_frontend::common ptx_frontend::semantic fmt::fmt
)
add_dependencies(ptx_frontend_resolved_ir resolved_ir_codegen)

if(PTX_FRONTEND_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

install(
    DIRECTORY "${PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

if(BUILD_TESTING)
    set(tensor_map_known_facts_fixture
        "${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}/ptx_frontend/resolved_ir/test/tensor_map_known_facts_cases.gen.inc")
    add_custom_command(
        OUTPUT "${tensor_map_known_facts_fixture}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}/ptx_frontend/resolved_ir/test"
        COMMAND ${CMAKE_COMMAND} -E env
            "PYTHONPATH=${PROJECT_SOURCE_DIR}:${PROJECT_SOURCE_DIR}/python/src"
            ${Python3_EXECUTABLE} -m python.tests.spec.test_tensor_map_known_facts
            --emit-cpp-fixtures "${tensor_map_known_facts_fixture}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/python/tests/spec/test_tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/code_gen/emit/tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/resolved_ir.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/tensor_reduction.py"
        VERBATIM
    )
    add_custom_target(tensor_map_known_facts_test_fixtures
        DEPENDS "${tensor_map_known_facts_fixture}")
    file(GLOB resolved_ir_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    list(REMOVE_ITEM resolved_ir_test_srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_modern_operand_codegen.cpp")
    add_executable(test_resolved_ir ${resolved_ir_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_resolved_ir
        PRIVATE GTest::gtest GTest::gtest_main ptx_frontend::resolved_ir
    )
    target_include_directories(test_resolved_ir PRIVATE
        "${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}")
    add_dependencies(test_resolved_ir tensor_map_known_facts_test_fixtures)
    # Keep source locations for test failures without full variable/type debug info.
    target_compile_options(test_resolved_ir PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-gline-tables-only>
    )
    gtest_discover_tests(test_resolved_ir)
endif()
