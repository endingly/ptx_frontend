# PTX 9.3 audit probes

These finite fixtures exercise audit candidates against source commit `1b44ad8212aa254610b3afb447c9e774df03710d`. They are evidence, not additions to the production test suite or a claim of ISA coverage. Only `probe.cpp`, this CMake hook, fixtures, and results are retained; builds and dependencies live under `/tmp/ptx93-audit-*`.

`probe.cpp` parses a complete module and then calls `resolveAndValidateModule`. It prints diagnostics and returns 0 for clean success, 2 for parse/lowering diagnostics, 3 for resolution/validation diagnostics, or 4 for a caught exception. `--parse-only` stops after parsing. The `ptx93_parse_probe` target always stops after parsing. Diagnostic ranges are the frontend's reported coordinates.

## Reproduce

Supply GCC 14 with C++23, CMake >=3.28, Ninja, Flex 2.6.4, Python >=3.14 with the packages in `python/pyproject.toml`, fmt, and magic_enum. No GTest dependency is needed with `BUILD_TESTING=OFF`. This run installed Python dependencies in a temporary venv; fmt 11.2.0 and magic_enum v0.9.7 were built from their official GitHub repositories. Flex and m4 were extracted from official Debian `.deb` files into a temporary prefix. No system files or repository build files were changed.

From the repository root, adjusting the dependency prefix/interpreter and executables to local paths:

```sh
cmake -S . -B /tmp/ptx93-audit-build -G Ninja \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/docs/audits/ptx93_probes/probe-hook.cmake" \
  -DCMAKE_PREFIX_PATH=/tmp/ptx93-audit-deps/prefix \
  -DPython3_EXECUTABLE=/tmp/ptx93-audit-tools/venv/bin/python \
  -DCMAKE_CXX_FLAGS='-O0 -g0' -DBUILD_TESTING=OFF \
  -DPTX_USE_CCACHE=OFF -DPTX_FRONTEND_CODEGEN_JOBS=4
cmake --build /tmp/ptx93-audit-build --target ptx93_parse_probe ptx93_probe -j4
python3 docs/audits/ptx93_probes/run_batch.py \
  --probe /tmp/ptx93-audit-build/ptx93_probe --output /tmp/ptx93-audit-results.txt \
  docs/audits/ptx93_probes/fixtures/*.ptx
```

The original build additionally set `CMAKE_MAKE_PROGRAM` to the temporary Ninja executable and put extracted Flex/m4 and venv tools on `PATH`; `M4` pointed to the extracted m4 executable. Configure, both focused targets, and all fixture runs completed successfully. The production libraries were compiled with `-O0 -g0`, four compile jobs, and four generator jobs. No full test suite was run.

## Observations

- The valid, malformed syntax, and unresolved identifier self-checks return 0, 2, and 3 respectively.
- `.global .v2 .f64` passes; `.global .v4 .f64` is correctly rejected by declaration semantics for exceeding the 128-bit storage vector limit. This candidate was refuted.
- Historical PTX 1.0 `.v4.u16` whole `%tid` MOV is rejected; the PTX 2.0 `.v4.u32` control passes. The official historical definition and current vector MOV text need semantic adjudication; runtime difference alone is not a bug finding.
- `%pm0_64` with `mov.u32` is rejected for its U64 declared type; `mov.u64` passes. Conflicting manual examples are not sufficient to label this a frontend bug.
- `bmsk.wrap.b32` widths 2 and 33 both pass. This agrees with the modeled full 32-bit operand domain; no compiler defect is established by the candidate.
- A single metadata `.file 1` passes; repeating file index 1 is rejected as a duplicate symbol. The manual's example list does not establish that the listed alternatives must form one valid program.

## Additional finite obligations

`expectations.json` records the expected result, official manual anchor, reasoning, and observation for nine additional fixtures. All matched their expectations after reading the cited sections: non-register predicate vector and oversized register vector rejection; exact/inferred array initialization acceptance and excess-element rejection; `%envreg31` acceptance versus `%envreg32` rejection; and `.address_size 64` acceptance versus `.address_size 16` rejection. These fixtures were executed once each after the exported hook was reconfigured and both probe targets rebuilt successfully. The invalid address-size diagnostic category is `ModuleSourceMismatch`; rejection itself matches §11.1.3.

## Tool provenance

```text
1b44ad8212aa254610b3afb447c9e774df03710d
cmake version 4.4.4

CMake suite maintained and supported by Kitware (kitware.com/cmake).
1.13.2.git.kitware.jobserver-pipe-1
Python 3.14.10
c++ (Debian 14.2.0-19) 14.2.0
Copyright (C) 2024 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

flex 2.6.4
40626af88bd7df9a5fb80be7b25ac85b122d6c21
e046b69a3736d314fad813e159b1c192eaef92cd
75f963765232c356d0a2fb70e780dfa9de2425aca036ec60eefb7fc98a2c905b  /tmp/ptx93-audit-deps/flex.deb
221b3e224708c1da2a1a88b6824380593579b2254ae7d914afa14cbd5d343334  /tmp/ptx93-audit-deps/m4.deb
```
