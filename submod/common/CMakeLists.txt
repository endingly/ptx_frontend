add_library(ptx_frontend_common INTERFACE)
set_property(TARGET ptx_frontend_common PROPERTY EXPORT_NAME common)
add_library(ptx_frontend::common ALIAS ptx_frontend_common)
target_compile_features(ptx_frontend_common INTERFACE cxx_std_23)
register_headers(
    ptx_frontend_common
    common
    INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
