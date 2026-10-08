find_package(benchmark CONFIG REQUIRED)

get_filename_component(_default_corpus
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../corpus/m12/natural_kernel_sm80.ptx" ABSOLUTE)
set(PTX_BENCHMARK_CORPUS_FILE "${_default_corpus}" CACHE FILEPATH
    "PTX source used by the resolved IR benchmark cases")

add_executable(frontend_symbol_table_scaling symbol_table_scaling.cpp)
target_compile_features(frontend_symbol_table_scaling PRIVATE cxx_std_23)
target_link_libraries(frontend_symbol_table_scaling PRIVATE
    ptx_frontend::resolved_ir benchmark::benchmark)
target_compile_definitions(frontend_symbol_table_scaling PRIVATE
    PTX_BENCHMARK_CORPUS_PATH="${PTX_BENCHMARK_CORPUS_FILE}")
