# Tensor Memory transfer installed consumer

This small standalone program links the installed `resolved_ir` package. It
checks a 128-register split load, source-only split offset, reduction, both
wait classes, and module validation after the syntax AST is destroyed.

After installing the project into an isolated prefix, configure this
directory with `-DCMAKE_PREFIX_PATH=<install-prefix>`, build it, and run
`tensor_memory_transfer_consumer`.
