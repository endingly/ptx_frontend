# Installed stack consumer

This independent CMake project uses installed public headers and canonical resources. It checks all eight stack source forms after destroying the parser and AST, alignment conversion and presence, typed local results, and forged function context rejection.

```sh
cmake -S examples/stack_consumer -B out/stack-consumer \
  -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build out/stack-consumer
out/stack-consumer/stack_consumer
```

The consumer also needs `fmt` and `magic_enum` CMake configurations. Make them
available through the same vcpkg toolchain or add its installed dependency
prefix to `CMAKE_PREFIX_PATH` alongside the frontend installation.
