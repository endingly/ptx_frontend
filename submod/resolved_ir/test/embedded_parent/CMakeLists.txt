cmake_minimum_required(VERSION 3.28)
project(ptx_frontend_embedded_parent LANGUAGES CXX)

if(NOT DEFINED PTX_FRONTEND_SOURCE_DIR)
    message(FATAL_ERROR "PTX_FRONTEND_SOURCE_DIR is required")
endif()

# These target names belong to the embedding project. They must not collide
# with the frontend's private component-library targets.
foreach(_parent_target IN ITEMS
        common base lexer cst syntax binding semantic resolved_ir)
    add_library(${_parent_target} INTERFACE)
endforeach()

add_subdirectory("${PTX_FRONTEND_SOURCE_DIR}" ptx_frontend)

foreach(_component IN ITEMS
        common base lexer cst syntax binding semantic resolved_ir)
    if(NOT TARGET ptx_frontend_${_component})
        message(FATAL_ERROR "Missing project-prefixed target: ptx_frontend_${_component}")
    endif()
    if(NOT TARGET ptx_frontend::${_component})
        message(FATAL_ERROR "Missing stable alias: ptx_frontend::${_component}")
    endif()
endforeach()

get_target_property(_common_compile_features
    ptx_frontend_common INTERFACE_COMPILE_FEATURES)
if(NOT "cxx_std_23" IN_LIST _common_compile_features)
    message(FATAL_ERROR "ptx_frontend::common lost its C++23 usage requirement")
endif()

add_executable(ptx_frontend_embedded_parent_consumer main.cpp)
target_link_libraries(ptx_frontend_embedded_parent_consumer
    PRIVATE ptx_frontend::ptx_frontend)
