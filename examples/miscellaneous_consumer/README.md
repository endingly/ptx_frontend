# Miscellaneous instruction installed consumer

This example configures against an installed `ptx_frontend::resolved_ir` target.
It resolves all five miscellaneous opcodes, retains register and immediate
`nanosleep` sources and both `setmaxnreg` actions, and verifies narrowing of
an integer duration. It validates the owned module after the parser and AST
are destroyed, then rejects both mismatched event bits and an invalid index.

Install the library to a fresh prefix, then configure with
`-Dptx_frontend_DIR=<prefix>/lib/cmake/ptx_frontend` and a
`CMAKE_PREFIX_PATH` containing both that prefix and its dependency prefix.
Build and run `miscellaneous_consumer`.
