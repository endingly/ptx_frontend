include_guard(GLOBAL)

# Install and export a relocatable CMake package.
#
# Usage:
# install_project_targets(
#   PROJECT <package-name>
#   VERSION <major.minor.patch>
#   NAMESPACE <imported-target-namespace>
#   TARGETS <target>...
#   COMPONENTS <find-package-component>...
#   INCLUDE_DIRS <source-include-root>...
#   INCLUDE_DESTINATIONS <install-include-directory>...
#   DEPENDENCIES <find_dependency-arguments>...
# )
function(install_project_targets)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    cmake_parse_arguments(
        INSTALL_PROJECT
        ""
        "PROJECT;VERSION;NAMESPACE"
        "TARGETS;COMPONENTS;INCLUDE_DIRS;INCLUDE_DESTINATIONS;DEPENDENCIES"
        ${ARGN}
    )

    if(INSTALL_PROJECT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "install_project_targets: unexpected arguments: "
            "${INSTALL_PROJECT_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT INSTALL_PROJECT_PROJECT)
        message(FATAL_ERROR
            "install_project_targets: PROJECT argument is required")
    endif()
    if(NOT INSTALL_PROJECT_VERSION)
        message(FATAL_ERROR
            "install_project_targets: VERSION argument is required")
    endif()
    if(NOT INSTALL_PROJECT_NAMESPACE)
        set(INSTALL_PROJECT_NAMESPACE "${INSTALL_PROJECT_PROJECT}")
    endif()
    if(NOT INSTALL_PROJECT_TARGETS)
        message(FATAL_ERROR
            "install_project_targets: TARGETS argument is required")
    endif()

    list(LENGTH INSTALL_PROJECT_INCLUDE_DIRS _include_dir_count)
    list(LENGTH INSTALL_PROJECT_INCLUDE_DESTINATIONS
         _include_destination_count)
    if(NOT _include_dir_count EQUAL _include_destination_count)
        message(FATAL_ERROR
            "install_project_targets: INCLUDE_DIRS and "
            "INCLUDE_DESTINATIONS must contain the same number of entries")
    endif()

    set(_install_cmake_dir
        "${CMAKE_INSTALL_LIBDIR}/cmake/${INSTALL_PROJECT_PROJECT}")
    set(_export_name "${INSTALL_PROJECT_PROJECT}Targets")

    install(
        TARGETS ${INSTALL_PROJECT_TARGETS}
        EXPORT ${_export_name}
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    if(_include_dir_count GREATER 0)
        math(EXPR _last_include_index "${_include_dir_count} - 1")
        foreach(_index RANGE 0 ${_last_include_index})
            list(GET INSTALL_PROJECT_INCLUDE_DIRS ${_index} _include_dir)
            list(GET INSTALL_PROJECT_INCLUDE_DESTINATIONS ${_index}
                 _include_destination)
            install(
                DIRECTORY "${_include_dir}/"
                DESTINATION "${_include_destination}"
                FILES_MATCHING
                    PATTERN "*.h"
                    PATTERN "*.hpp"
                    PATTERN "*.inl"
                    PATTERN "*.ipp"
                    PATTERN "*.def"
            )
        endforeach()
    endif()

    set(PACKAGE_DEPENDENCY_CALLS "")
    foreach(_dependency IN LISTS INSTALL_PROJECT_DEPENDENCIES)
        string(APPEND PACKAGE_DEPENDENCY_CALLS
               "find_dependency(${_dependency})\n")
    endforeach()
    set(PACKAGE_COMPONENTS "${INSTALL_PROJECT_COMPONENTS}")
    set(PACKAGE_NAME "${INSTALL_PROJECT_PROJECT}")
    set(PACKAGE_VERSION "${INSTALL_PROJECT_VERSION}")
    set(PTX_FRONTEND_PTX_SPEC_DIR
        "${CMAKE_INSTALL_DATADIR}/ptx_frontend/ptx_spec")
    set(PTX_FRONTEND_PTX_SPEC_SCHEMA
        "${CMAKE_INSTALL_DATADIR}/ptx_frontend/ptx-instr-v1.schema.yaml")

    set(_template
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${INSTALL_PROJECT_PROJECT}Config.cmake.in")
    if(NOT EXISTS "${_template}")
        message(FATAL_ERROR
            "install_project_targets: package config template not found: "
            "${_template}")
    endif()

    set(_config_out
        "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Config.cmake")
    set(_version_out
        "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}ConfigVersion.cmake")
    set(_targets_out
        "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Targets.cmake")

    configure_package_config_file(
        "${_template}"
        "${_config_out}"
        INSTALL_DESTINATION "${_install_cmake_dir}"
        PATH_VARS
            PTX_FRONTEND_PTX_SPEC_DIR
            PTX_FRONTEND_PTX_SPEC_SCHEMA
        NO_SET_AND_CHECK_MACRO
    )
    write_basic_package_version_file(
        "${_version_out}"
        VERSION "${INSTALL_PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion
    )

    export(
        EXPORT ${_export_name}
        FILE "${_targets_out}"
        NAMESPACE "${INSTALL_PROJECT_NAMESPACE}::"
    )
    install(
        EXPORT ${_export_name}
        FILE "${INSTALL_PROJECT_PROJECT}Targets.cmake"
        NAMESPACE "${INSTALL_PROJECT_NAMESPACE}::"
        DESTINATION "${_install_cmake_dir}"
    )
    install(
        FILES "${_config_out}" "${_version_out}"
        DESTINATION "${_install_cmake_dir}"
    )
endfunction()
