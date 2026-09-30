cmake_minimum_required(VERSION 3.28)
project(ptx_owned_ir_experiment LANGUAGES CXX)

find_package(ptx_frontend CONFIG REQUIRED)

add_executable(ptx_owned_ir_runtime runtime.cpp)
target_compile_features(ptx_owned_ir_runtime PRIVATE cxx_std_23)
target_link_libraries(ptx_owned_ir_runtime PRIVATE ptx_frontend::resolved_ir)
