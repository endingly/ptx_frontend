# Tensor cache controls installed consumer

This installed-client smoke test parses a tensor load with a written
`.L2::cache_hint` and a final B64 policy register, resolves and validates the
owned module, then copies selected hint/policy information through the public
query after parser and module destruction. The register spelling is used only
to check provenance; no policy bits are decoded.

After installation, configure this directory against the installed
`ptx_frontend` package:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build
./build/tensor_cache_controls_consumer
```

The formal syntax also permits hint-only forms. The pinned PTXAS 13.3.73
corpus rejected the tested hint-only spellings, so this consumer uses the
matched hint-plus-policy form. See
[English cache coverage](../../docs/us-en/tensor_cache_controls.md) and
[中文缓存控制说明](../../docs/zh-han/tensor_cache_controls.md).
