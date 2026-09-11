cmake_minimum_required(VERSION 3.28)

# Standalone mode compares the same driver against separately installed revisions.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_filename_component(_frontend_source "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(VCPKG_MANIFEST_DIR "${_frontend_source}" CACHE PATH "Frontend dependency manifest")
    list(APPEND VCPKG_MANIFEST_FEATURES benchmarks)
    list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
    project(ptx_frontend_benchmarks LANGUAGES CXX)
    find_package(ptx_frontend CONFIG REQUIRED)
endif()

find_package(benchmark CONFIG REQUIRED)
get_filename_component(_default_corpus
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../corpus/m12/natural_kernel_sm80.ptx" ABSOLUTE)
set(PTX_BENCHMARK_CORPUS_FILE "${_default_corpus}" CACHE FILEPATH
    "PTX source used by the corpus benchmark cases")
add_executable(frontend_symbol_table_scaling symbol_table_scaling.cpp)
target_compile_features(frontend_symbol_table_scaling PRIVATE cxx_std_23)
target_link_libraries(frontend_symbol_table_scaling PRIVATE
    ptx_frontend::resolved_ir benchmark::benchmark)
target_compile_definitions(frontend_symbol_table_scaling PRIVATE
    PTX_BENCHMARK_CORPUS_PATH="${PTX_BENCHMARK_CORPUS_FILE}")
