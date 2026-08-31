include_guard(GLOBAL)

function(install_codegen_components)
    include(GNUInstallDirs)

    set(_data_install_dir "${CMAKE_INSTALL_DATADIR}/ptx_frontend")
    set(_cmake_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/ptx_frontend")
    set(_resource_dir "${PROJECT_SOURCE_DIR}/python/code_gen/resources")

    install(
        DIRECTORY "${_resource_dir}/ptx_spec/"
        DESTINATION "${_data_install_dir}/ptx_spec"
        COMPONENT ptx_spec
        FILES_MATCHING PATTERN "*.yaml"
    )
    install(
        FILES "${_resource_dir}/ptx-instr-v1.schema.yaml"
        DESTINATION "${_data_install_dir}"
        COMPONENT ptx_spec
    )

    install(
        FILES "${PROJECT_SOURCE_DIR}/cmake/ptx_frontendCodegen.cmake"
        DESTINATION "${_cmake_install_dir}"
        COMPONENT codegen
    )
endfunction()
