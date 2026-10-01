# Installed Tensor Memory allocation consumer

This consumer links the installed `ptx_frontend::resolved_ir` package. It
checks all three allocation-management actions, releases the syntax tree, and
then validates the owned module. Two kernels use different CTA groups to
exercise the per-body group boundary.

After installing the Debug library to a fresh prefix, configure, build, and
run this directory against that prefix and its compatible dependency prefix:

```sh
cmake -S examples/tensor_memory_allocation_consumer -B /tmp/tcgen-allocation-consumer \
  -Dptx_frontend_DIR="$fresh_prefix/lib/cmake/ptx_frontend" \
  -DCMAKE_PREFIX_PATH="$fresh_prefix;$dependency_prefix"
cmake --build /tmp/tcgen-allocation-consumer --parallel 1
/tmp/tcgen-allocation-consumer/tensor_memory_allocation_consumer
```
