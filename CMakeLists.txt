cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

aux_source_directory(src ptx_frontend_srcs)
add_library(ptx_frontend ${ptx_frontend_srcs})
target_include_directories(ptx_frontend PUBLIC include)