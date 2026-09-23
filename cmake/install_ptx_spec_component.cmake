include_guard(GLOBAL)

function(install_ptx_spec_component)
    include(GNUInstallDirs)

    set(_data_install_dir "${CMAKE_INSTALL_DATADIR}/ptx_frontend")
    set(_resource_dir "${PROJECT_SOURCE_DIR}/python/src/ptx_frontend/spec/resources")
    set(_backend_spec_dir "${PROJECT_SOURCE_DIR}/instructions/ptx_cpp_backend_spec")

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
        FILES "${_backend_spec_dir}/ptx_frontend.yaml"
        DESTINATION "${_data_install_dir}/ptx_cpp_backend_spec"
        COMPONENT ptx_spec
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/instructions/ptx-cpp-backend-v2.schema.yaml"
        DESTINATION "${_data_install_dir}"
        COMPONENT ptx_spec
    )
endfunction()
