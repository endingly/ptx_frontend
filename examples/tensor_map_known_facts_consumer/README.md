# Tensor-map known-facts installed consumer

Configure this example against a fresh installation of
`ptx_frontend::resolved_ir`, then build and run `tensor_map_known_facts_consumer`.
It uses only installed public headers and the exported library.

The example supplies facts independently. A Checked box relation and a
NotApplicable no-swizzle atomicity relation coexist with unknown dimensions
and addresses. The report cannot authenticate the bytes behind a tensor-map
reference or prove that a GPU access will succeed.
