cmake_minimum_required(VERSION 3.28.0)

project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

aux_source_directory(src ptx_frontend_srcs)
add_library(ptx_frontend ${ptx_frontend_srcs})
target_include_directories(ptx_frontend PUBLIC include)

## target ptx_frontend_test
aux_source_directory(test ptx_frontend_test_files)
add_executable(ptx_frontend_test ${ptx_frontend_test_files})
target_link_libraries(ptx_frontend_test PRIVATE ptx_frontend)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(ptx_frontend_test PRIVATE GTest::gtest GTest::gtest_main)