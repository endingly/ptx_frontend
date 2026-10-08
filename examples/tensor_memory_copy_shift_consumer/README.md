# Installed Tensor Memory copy/shift consumer

This standalone consumer uses the installed `ptx_frontend::resolved_ir` package. It checks exact final copy and shift classes, both written copy-format locations, a borrowed opaque Table 43 descriptor view after AST release, the shift source-order alias, and the group-qualified commit identity.

```sh
cmake -S examples/tensor_memory_copy_shift_consumer -B /tmp/tcgen-copy-consumer -DCMAKE_PREFIX_PATH=/path/to/installed/ptx_frontend
cmake --build /tmp/tcgen-copy-consumer --parallel 2
/tmp/tcgen-copy-consumer/tensor_memory_copy_shift_consumer
```
