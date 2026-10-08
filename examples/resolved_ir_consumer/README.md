# Installed resolved IR consumer

This consumer checks the installed `resolved_ir` CMake component,
one representative exact-form resolver/checker path, strict module resolution
and validation, and the installed `ptx_spec` resource paths. It is an
installation smoke test, not the full
module semantic suite.

```sh
cmake --install out/build/ci-linux-clang-debug --prefix /tmp/ptx-resolved-install
cmake -S examples/resolved_ir_consumer -B /tmp/ptx-resolved-consumer \
  -DCMAKE_PREFIX_PATH=/tmp/ptx-resolved-install
cmake --build /tmp/ptx-resolved-consumer
/tmp/ptx-resolved-consumer/resolved_ir_consumer
```
