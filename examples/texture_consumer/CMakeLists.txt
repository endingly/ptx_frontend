cmake_minimum_required(VERSION 3.24)
project(texture_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ptx_frontend 0.7.0 CONFIG REQUIRED COMPONENTS resolved_ir)

add_executable(texture_consumer main.cpp)
target_link_libraries(texture_consumer PRIVATE
    ptx_frontend::resolved_ir)
