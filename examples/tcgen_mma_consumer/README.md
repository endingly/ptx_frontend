# Installed TCGEN dense f16 MMA consumer

This out-of-tree consumer uses the installed resolved-IR package. It checks the
immutable Table 42 shape/datapath rows and a pure caller-known-word report.
It then resolves a complete dense f16 MMA module, destroys the syntax AST, and
checks the selected borrowed A/B descriptor roles, predicate negation, mask,
scale, completion-compatible commit, and module validity. A missing B shared
word remains an obligation; the source register's bits are never inferred.

Configure this directory with the installation prefix in `CMAKE_PREFIX_PATH`,
build `tcgen_mma_consumer`, and run the binary. This verifies the frontend
contract and linking across generated sources, without GPU execution or
runtime descriptor-content proof.
