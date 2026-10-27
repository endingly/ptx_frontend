cmake_minimum_required(VERSION 3.24)
project(memory_cache_hint_consumer LANGUAGES CXX)

find_package(ptx_frontend 0.7.0 CONFIG REQUIRED COMPONENTS resolved_ir)

add_executable(memory_cache_hint_consumer main.cpp)
target_compile_features(memory_cache_hint_consumer PRIVATE cxx_std_23)
target_link_libraries(memory_cache_hint_consumer PRIVATE ptx_frontend::resolved_ir)
