# Active direct-class resolved IR; generation shares the canonical lowering plan.
find_package(fmt CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(ptx_resolved_ir_codegen)
ptx_configure_resolved_ir_codegen(
    resolved_generated_sources
    resolved_generated_private
    resolved_generated_public)

add_library(ptx_frontend_resolved_ir STATIC
    ${resolved_generated_sources}
    src/ptx_resolved_ir.cpp
    src/ptx_resolved_ir_modifiers.cpp
    src/ptx_resolved_ir_literals.cpp
    src/ptx_resolved_ir_bound_operands.cpp
    src/ptx_resolved_ir_layout.cpp
    src/ptx_resolved_ir_checker.cpp
    src/ptx_named_array_address.cpp
    src/ptx_video.cpp
    src/ptx_storage_declarations.cpp
    src/ptx_resolved_module.cpp
    src/ptx_module_availability.cpp
    src/ptx_resolved_function.cpp)
set_property(TARGET ptx_frontend_resolved_ir
    PROPERTY EXPORT_NAME resolved_ir)
add_library(ptx_frontend::resolved_ir
    ALIAS ptx_frontend_resolved_ir)
register_headers(ptx_frontend_resolved_ir resolved_ir
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ptx_frontend_resolved_ir
    PUBLIC $<BUILD_INTERFACE:${resolved_generated_public}>
    PRIVATE "${resolved_generated_private}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ptx_frontend_resolved_ir PUBLIC
    ptx_frontend::base ptx_frontend::binding ptx_frontend::syntax
    ptx_frontend::common ptx_frontend::semantic fmt::fmt)
add_dependencies(ptx_frontend_resolved_ir
    resolved_ir_codegen)
if(PTX_FRONTEND_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
if(BUILD_TESTING)
    set(tensor_map_known_facts_fixture
        "${resolved_generated_private}/ptx_frontend/resolved_ir/test/tensor_map_known_facts_cases.gen.inc")
    add_custom_command(
        OUTPUT "${tensor_map_known_facts_fixture}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${resolved_generated_private}/ptx_frontend/resolved_ir/test"
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${PROJECT_SOURCE_DIR}:${PROJECT_SOURCE_DIR}/python/src"
            "${Python3_EXECUTABLE}" -m python.tests.spec.test_tensor_map_known_facts
            --emit-cpp-fixtures "${tensor_map_known_facts_fixture}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/python/tests/spec/test_tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/code_gen/emit/tensor_map_known_facts.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/resolved_ir.py"
            "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/ir/tensor_reduction.py"
        VERBATIM)
    add_custom_target(tensor_map_known_facts_test_fixtures
        DEPENDS "${tensor_map_known_facts_fixture}")
    add_executable(resolved_ir_smoke test/smoke.cpp)
    target_link_libraries(resolved_ir_smoke PRIVATE
        ptx_frontend::resolved_ir)
    add_test(NAME resolved_ir_smoke
        COMMAND resolved_ir_smoke)

    find_package(GTest CONFIG REQUIRED)
    file(GLOB resolved_test_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
    list(REMOVE_ITEM resolved_test_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.cpp")
    add_executable(test_resolved_ir ${resolved_test_sources})
    target_link_libraries(test_resolved_ir PRIVATE
        GTest::gtest GTest::gtest_main ptx_frontend::resolved_ir)
    target_compile_options(test_resolved_ir PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:Clang>>:-gline-tables-only>)
    target_include_directories(test_resolved_ir PRIVATE
        "${resolved_generated_private}")
    add_dependencies(test_resolved_ir tensor_map_known_facts_test_fixtures)
    gtest_discover_tests(test_resolved_ir)
endif()
