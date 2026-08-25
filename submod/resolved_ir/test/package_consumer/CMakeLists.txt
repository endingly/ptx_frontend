cmake_minimum_required(VERSION 3.28)
project(ptx_frontend_package_consumer LANGUAGES CXX)

find_package(ptx_frontend CONFIG REQUIRED)
enable_testing()

add_executable(ptx_frontend_package_consumer main.cpp)
target_compile_features(ptx_frontend_package_consumer PRIVATE cxx_std_23)
target_link_libraries(
    ptx_frontend_package_consumer
    PRIVATE ptx_frontend::ptx_frontend
)
add_test(NAME runs_installed_frontend COMMAND ptx_frontend_package_consumer)
