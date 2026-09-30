# Installed warp-matrix consumer

This small consumer links only the installed `ptx_frontend::resolved_ir` package. It parses a block-scaled warp MMA and WGMMA forms on both sides of a generated 64-form shard boundary, releases each syntax tree, and checks their public owned matrix descriptors, operands, and protocol controls.

After installing a Debug build to a fresh prefix, configure and run it with that package and the same vcpkg dependency prefix:

```sh
cmake -S examples/matrix_consumer -B /tmp/matrix-consumer-build \
  -Dptx_frontend_DIR="$fresh_prefix/lib/cmake/ptx_frontend" \
  -DCMAKE_PREFIX_PATH="$fresh_prefix;$PWD/out/build/ci-linux-clang-debug/vcpkg_installed/x64-linux"
cmake --build /tmp/matrix-consumer-build --parallel 1
/tmp/matrix-consumer-build/matrix_consumer
```
