include_guard(GLOBAL)

function(install_codegen_components)
    include(GNUInstallDirs)

    set(_data_install_dir "${CMAKE_INSTALL_DATADIR}/ptx_frontend")
    set(_codegen_install_dir "${_data_install_dir}/codegen")
    set(_python_install_dir "${_codegen_install_dir}/python")
    set(_cmake_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/ptx_frontend")
    set(_resource_dir "${PROJECT_SOURCE_DIR}/python/code_gen/resources")

    # ptx_spec is a public data component whose canonical source also lives in
    # the Python package. Installing only ptx_spec therefore stages the same
    # resource paths that a full/codegen install exposes.
    install(
        DIRECTORY "${_resource_dir}/ptx_spec/"
        DESTINATION "${_python_install_dir}/code_gen/resources/ptx_spec"
        COMPONENT ptx_spec
        FILES_MATCHING PATTERN "*.yaml"
    )
    install(
        FILES "${_resource_dir}/ptx-instr-v1.schema.yaml"
        DESTINATION "${_python_install_dir}/code_gen/resources"
        COMPONENT ptx_spec
    )

    foreach(_python_package IN ITEMS base code_gen ir)
        install(
            DIRECTORY "${PROJECT_SOURCE_DIR}/python/${_python_package}/"
            DESTINATION "${_python_install_dir}/${_python_package}"
            COMPONENT codegen
            FILES_MATCHING
                PATTERN "*.py"
                PATTERN "*.yaml"
                PATTERN "__pycache__" EXCLUDE
        )
    endforeach()
    install(
        FILES "${PROJECT_SOURCE_DIR}/python/scripts/gen_all.py"
        DESTINATION "${_python_install_dir}/scripts"
        COMPONENT codegen
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/cmake/ptx_frontendCodegen.cmake"
        DESTINATION "${_cmake_install_dir}"
        COMPONENT codegen
    )
endfunction()
