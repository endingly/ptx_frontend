# Installed vector formal consumer

After installing the frontend, configure this standalone project with its install
prefix in `CMAKE_PREFIX_PATH`, build, and run `vector_formal_consumer`. It reads
owned input/return vector shapes after source and AST destruction, validates a
named-vector call, and verifies rejection of a corrupted signature shape.
