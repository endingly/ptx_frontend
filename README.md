# PTX Frontend

PTX Frontend is an experimental C++23 frontend for parsing and analysing a
deliberately limited subset of NVIDIA PTX. It provides a lossless concrete
syntax tree, a typed syntax AST, lexical symbol binding, declaration checks,
and generated Resolved IR with target-aware checking for selected opcodes.

> **Status:** pre-1.0. The public API and generated IR types may change without
> notice. This is not a complete PTX ISA implementation, assembler, or code
> generator.

## Current scope

The frontend currently provides:

- tokenization and CST parsing with source locations and retained source text;
- Syntax AST lowering for supported module, function, declaration, expression,
  initializer, and instruction forms;
- module/function symbol scopes, reference classification, and declaration
  semantics;
- generated resolution and checking for YAML-modelled bare `ret`/`exit`/`trap` (with ordinary predicate guards accepted), fixed `and.b32`/`or.b32`/`xor.b32`/`not.b32`/`shl.b32`/`shr.u32`/`mul.lo.u32`/`mul.rn.f32`/`mad.lo.u32`/`fma.rn.f32`/`div.u32`, frozen `setp.lt{.and}.u32` single-predicate forms, `selp.u32`, register-only `cvta{.to}.global.u64`, `cvt.s32.u32`/`cvt.rn.f32.f64`/`cvt.rn.f32.u32`/`cvt.rzi.u32.f32`, `bra`, `brx.idx`, `add`,
  `sub`, `bar`, selected `mov` forms, generic/basic-explicit scalar `ld`/`st` across
  the 14 `.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`,
  `.f32/.f64` types, plus legacy `.v2/.v4` braced-vector `ld`/`st` and the
  PTX 8.8/SM 100 256-bit modern forms (`.v8` × 32-bit or `.v4` × 64-bit);
  modern vectors require global space when known and permit partial `_` sinks,
  while legacy vectors remain limited to 128 bits and reject sinks. Legacy `ld`
  cache operators
  `.ca/.cg/.cs/.lu/.cv`, legacy `st` cache operators
  `.wb/.cg/.cs/.wt`, explicit `.const/.global/.local/.param/.shared` loads,
  and explicit `.global/.local/.param/.shared` stores, including data-driven
  `.weak/.volatile/.relaxed.scope/.acquire.scope/.release.scope` consistency
  qualifiers and PTX 8.2 `.mmio.relaxed.sys` (legacy vectors intentionally
  exclude mmio),
  generic bound-space policies, exact explicit bound-symbol address-space
  checks, and input/return direction checks for bound `.param` addresses;
  operand register compatibility permits PTX's wider load destination/store
  source forms while retaining bit/integer/float kind restrictions for declared
  registers through 64 bits; wider `.b128` declarations remain deferred until
  declaration-type target availability is checked, and other instructions
  remain exact-width;
- explicit PTX ISA version, SM version, minimum family-specific source-feature
  target (`family`), exact target, and capability availability checks for
  modelled variants, modifiers, layouts, and operands. `family` checks the
  profile's `enabled_family_features`: `sm_100` and `sm_103` enable none;
  `sm_100f`/`sm_100a` enable `sm_100f`; `sm_103f`/`sm_103a` enable
  `sm_100f` and `sm_103f`; and `sm_120f` enables only `sm_120f`. This is a
  source-target feature requirement, not PTX-to-physical-GPU translation
  compatibility, which is not modelled. Exact identity and capability remain
  independent constraints.
- machine-readable manifests are authoritative for modelled opcode slices and
  deferred scope. M12 has two evidence lanes: the deterministic inline-PTX
  common-kernel corpus validates 60 frozen forms through parse, resolve, and
  target-aware checking on `sm_80`, `sm_90a`, and `sm_100`; its
  `setmaxnreg.inc.sync.aligned.u32` occurrence is only in the `sm_90a` corpus
  fixture. The separate ordinary-CUDA `natural_kernel` PTX outputs are frozen
  nvcc 13.3.33 compiler-emission evidence; their manifest preserves every
  emitted spelling and frequency, and all three profile modules parse,
  resolve, and target-aware check. Checker
  availability is broader: `sm_90a` at PTX 8.0, exact
  `sm_100a` at 8.6, the enabled `sm_100f` family at 8.8 (including modelled
  `sm_100f` and `sm_103a`/`sm_103f`), and `sm_120f` at 8.8. Official
  `sm_101a`/`sm_101f`, `sm_110a`/`sm_110f`, and `sm_120a` spellings are not
  catalogued validation profiles and therefore report `UnknownTarget`;
  translation compatibility is not inferred. Inventory rows remain partial for
  implemented frozen slices; simulator execution remains unsupported.
- direct `call` and metadata-backed indirect `call` forms, including:
  `call function;`, `call function, (arguments);`, and
  `call (return), function, (arguments);`. Module resolution uses the canonical
  prototype/definition signature to check return/input arity and order,
  `.reg/.param` type and vector shape, `.param .b8` array extent/alignment, and
  pointer state-space/alignment. Input literals are typed against their formal,
  including literal-kind and overflow diagnostics. Function-local `.param`
  staging also enforces PTX 9.3 `::entry`/`::func` qualification plus
  unpredicated contiguous store/call/load adjacency; the call itself may remain
  predicated. Metadata-backed indirect calls use a `.reg` target plus a
  function-local `.callprototype` or `.calltargets` declaration, share the
  same ABI checks, and require PTX 2.1 / SM 20. `brx.idx{.uni}` uses a `.u32`
  index and current-function `.branchtargets` identity at PTX 6.0 / SM 30;
  neither form expands metadata entries. Standalone instruction resolution has
  no callee context, so its call literals remain untyped.

These checks cover only the currently modelled instruction subset. They do not
validate the full PTX ISA, every module directive, or link-time behaviour.

Some constructs can be tokenized or represented in the Syntax AST without
being supported by Resolved IR. See the
[syntax coverage matrix](docs/us-en/syntax_coverage.md) for the exact current
boundary and known exclusions.

## Frontend pipeline

```text
PTX source
  -> lexer
  -> lossless CST
  -> Syntax AST
  -> symbol binding
  -> declaration semantics
  -> Resolved IR
  -> target-aware instruction checking
```

`PtxSyntaxParser` is the convenience source-to-AST facade. Use `PtxCstParser`
and the explicit lowering functions when retained tokens and source text are
required. `resolved_ir::resolveModule` runs binding and declaration checks as
part of module resolution and returns their diagnostics together with
instruction-resolution diagnostics.

## Requirements

- CMake 3.28 or newer;
- a C++23 compiler;
- Python 3 with the packages in `requirements.txt`;
- Flex and `clang-format` on `PATH`;
- `fmt` and `magic_enum` (`gtest` is also required when tests are enabled).

The checked-in presets use Ninja, GCC/G++, and the vcpkg manifest. Set
`VCPKG_ROOT` to an initialized vcpkg checkout before configuring.

## Build and test

Install the Python generator dependencies in your preferred environment:

```sh
python3 -m pip install -r requirements.txt
```

Configure, build, and run the C++ test suite:

```sh
cmake --preset ci-linux-gcc-debug
cmake --build --preset ci-linux-gcc-debug
ctest --preset ci-linux-gcc-debug --output-on-failure
```

Run the Python generator/model tests separately:

```sh
PYTHONPATH=python python3 -m unittest_parallel \
  -s python/tests -t python -p 'test_*.py' --level=module -v
```

The `ci-linux-gcc-release` preset provides an equivalent local Release
workflow; running it does not mean GitHub Actions was triggered.

## Use after installing

Build and install the package into the configured prefix:

```sh
cmake --preset ci-linux-gcc-debug
cmake --build --preset ci-linux-gcc-debug
cmake --install out/build/ci-linux-gcc-debug
```

Consumers can select one or more frontend components and link their namespaced
targets:

```cmake
find_package(
    ptx_frontend 0.0.1 CONFIG REQUIRED
    COMPONENTS resolved_ir
)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE ptx_frontend::resolved_ir)
```

Available components are `ptx_frontend`, `common`, `base`, `lexer`, `cst`,
`syntax`, `binding`, `semantic`, and `resolved_ir`. Each component is exposed
as `ptx_frontend::<component>`. The aggregate
`ptx_frontend::ptx_frontend` target links the complete frontend. The package
locates its public `fmt` and `magic_enum` dependencies, so those packages must
be reachable through the consumer's toolchain or package search prefix.

## Use from a source tree

The same namespaced targets are available when the project is added directly:

```cmake
add_subdirectory(path/to/ptx_frontend)

add_executable(example main.cpp)
target_compile_features(example PRIVATE cxx_std_23)
target_link_libraries(example PRIVATE ptx_frontend::ptx_frontend)
```

A minimal parse-and-resolve example:

```cpp
#include <iostream>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

int main() {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry kernel() {
  .reg .u32 %r<2>;
  add.u32 %r0, %r1, 1;
}
)ptx";

  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    for (const auto& diagnostic : ast.diagnostics)
      std::cerr << diagnostic.message << '\n';
    return 1;
  }

  auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!module) {
    for (const auto& diagnostic : module.error())
      std::cerr << diagnostic.message << '\n';
    return 1;
  }

  std::cout << "resolved functions: " << module->functions.size() << '\n';
}
```

Resolved instruction checking is a separate operation. Visit each
`ResolvedInstruction` and call `resolved_ir::checker::check` with a
`checker::Context` containing the intended PTX ISA version, SM version, target
families, and instruction source range.

## Generated code

The YAML files under `python/code_gen/resources/ptx_spec` are the canonical source of truth for the
currently generated opcode variants and descriptors. CMake invokes
`python/scripts/gen_all.py` during configuration to enumerate outputs and
during the build to generate the Resolved IR public header, runtime lookup
tables, dispatch, category implementations, and checker descriptors in the
`resolved_ir` build directory.

The generated files are build artifacts. Do not edit them or treat their
layout as a stable pre-1.0 ABI. See the
[YAML instruction specification](docs/us-en/yaml_instruction_spec.md) and
[Python generator model](docs/us-en/python_generator_model.md) before changing
the instruction database or generator.

`instructions/ptx_spec` is a source-tree compatibility symlink. Installed
packages also expose `ptx_spec` (the public PTX data) and `codegen` (the
generator plus its runtime resources) CMake components; consumers supply their
own backend mapping when generating C++.

## Repository layout

```text
submod/       C++ frontend stages and tests
instructions/ PTX instruction specs, backend mappings, and schemas
python/       normalization models, C++ generators, and Python tests
cmake/        shared CMake helpers
docs/us-en/   English design and coverage documentation
docs/zh-han/  Simplified Chinese design and coverage documentation
.agents/project_roadmap.v2.md  authoritative roadmap and implementation priority
docs/deprecated/next_step.md frozen historical implementation log
```

The [project roadmap](.agents/project_roadmap.v2.md) is the single source for
implementation status and priority. The
[deprecated next-step log](docs/deprecated/next_step.md) is historical only.

The detailed designs are documented in:

- [lexer](docs/us-en/lexer_design.md),
  [Syntax AST](docs/us-en/syntax_ast_design.md), and
  [control-flow syntax](docs/us-en/control_flow_syntax_design.md);
- [symbol binding](docs/us-en/symbol_binding_design.md) and
  [declaration semantics](docs/us-en/declaration_semantics_design.md);
- [Resolved IR and checker](docs/us-en/resolved_ir_design.md).

## License

This project is licensed under the [MIT License](LICENSE).
