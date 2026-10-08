cmake_minimum_required(VERSION 3.24)
project(tensor_cache_controls_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ptx_frontend 0.7.0 CONFIG REQUIRED COMPONENTS resolved_ir)

add_executable(tensor_cache_controls_consumer main.cpp)
target_compile_features(tensor_cache_controls_consumer PRIVATE cxx_std_23)
target_link_libraries(tensor_cache_controls_consumer PRIVATE
                      ptx_frontend::resolved_ir)
