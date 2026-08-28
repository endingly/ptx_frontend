cmake_minimum_required(VERSION 3.28.0)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
project(ptx_frontend VERSION 0.0.1 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(PTX_USE_CCACHE "Use ccache to speed up compilation" ON)
option(BUILD_TESTING "enable test program building" OFF)

if(PTX_USE_CCACHE)
    find_program(CCACHE_EXECUTABLE ccache)
    if(CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
    else()
        message(WARNING "PTX_USE_CCACHE is ON, but ccache was not found")
    endif()
endif()

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    include(GoogleTest)

    include(python_test)
endif()

add_subdirectory(submod)

add_library(ptx_frontend INTERFACE)
add_library(ptx_frontend::ptx_frontend ALIAS ptx_frontend)
target_link_libraries(ptx_frontend INTERFACE ptx_frontend::resolved_ir)

include(install_project_targets)
install_project_targets(
    PROJECT ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ptx_frontend
    TARGETS
        ptx_frontend
        common
        base
        lexer
        cst
        syntax
        binding
        semantic
        resolved_ir
    COMPONENTS
        ptx_frontend
        common
        base
        lexer
        cst
        syntax
        binding
        semantic
        resolved_ir
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/base/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/lexer/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/cst/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/syntax/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/binding/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/semantic/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/resolved_ir/include"
    INCLUDE_DESTINATIONS
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/common"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/base"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/lexer"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/cst"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/syntax"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/binding"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/semantic"
        "${CMAKE_INSTALL_INCLUDEDIR}/ptx_frontend/resolved_ir"
    DEPENDENCIES
        "fmt CONFIG"
        "magic_enum CONFIG"
)
