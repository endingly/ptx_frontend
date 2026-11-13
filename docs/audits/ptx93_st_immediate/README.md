# Immediate scalar ST compatibility investigation

The archived CUDA 13.3/PTX ISA 9.3 ST section explicitly says: “Operand b must
be in the .reg state space.” The natural nvcc 13.3.33 histogram output instead
contains five `st.shared.b32 [...], 0` instructions. This investigation records
NVIDIA compiler/assembler compatibility evidence, not normative manual support.
It makes no GPU execution or stored-value correctness claim.

The raw manual HTML SHA-256 is
`40026f79b65fee3f8c8b3e717c6f80c6385e8d97215318d6e4b7f6106f58d5a4`;
the extracted ST section SHA-256 is
`ff79a52b5da5d02c5a08d36967ea8264bd75552669659f1e18108bc064ccd201`.
The ptxas executable SHA-256 is
`7fdd01a4cf50e30746da98989c9272a907f491e6fd7fecfda14642e4375f88fb`.

`matrix.json` and `frontiers.json` retain exact commands, source hashes, exit
statuses and full diagnostics. Every immediate case has a matched register
control with the same instruction type and modifiers. PTX files and log files
are retained beside the JSON. Kernels preserve store effects: global/generic
stores are observable directly; shared/local stores are read back and written
to the output pointer. Parameter stores feed a call or function return whose
result is ultimately stored globally. Acceptance alone does not prove runtime
conversion semantics. `probe_matrix.py` and `probe_frontiers.py` reproduce the
finite probes with the recorded local tool path; they overwrite only this
probe phase's generated cases and JSON.

Observed scalar data producers at PTX 9.3/SM100:

| Target type | Integer tokens/expressions | Decimal float | 0f word | 0d word |
| --- | --- | --- | --- | --- |
| b/u/s8,16 | accepted | rejected | rejected | rejected |
| b/u/s32 | accepted | rejected | b32 only | rejected |
| b/u/s64 | accepted | b64 only | rejected | b64 only |
| b128 | accepted | rejected | rejected | rejected |
| f32/f64 | rejected | accepted | accepted | accepted |

Integer cases cover zero, nonzero, -1, 32/64-bit boundaries, and `(1+2)`.
ptxas accepts the above-uint64 decimal magnitude too; that is only a tool
observation, not permission to exceed the frontend's evaluated 64-bit domain.
Global/shared/local/generic samples preserve the same tested producer split.
Nonzero b32 immediates pass volatile, release/relaxed, .wb/.cg/.cs/.wt,
shared::cta/shared::cluster, explicit/generic L1::evict_last and L2::cache_hint
probes. Both `.param` and `.param::func` pass call staging and device returns.
An immediate cache-policy auxiliary operand also assembles; it is recorded
separately from the data-source investigation.

The oldest current ptxas target tested is SM75 at its PTX 6.3 floor; immediate
and register controls pass. PTX 6.4/7.0 SM75, PTX 7.0 SM80, PTX 8.0 SM90 and
PTX 8.6 SM100 also pass. SM50/SM70 invocations are classified as unsupported
assembler targets (both arms fail before instruction checking), not ST rejects.
Historical target acceptance is not established by this assembler version.


The follow-up `canonical-cache.json` adds canonical `st.global.wb/cg/cs/wt`
immediate/register pairs. The earlier matrix's `st.wb/cg/cs/wt.global` spellings
assemble but both frontend arms reject their modifier order; these raw cases
remain unchanged and are classified separately. No modifier-order alias was
added by the data-source change.

`frontend-comparison.json` records a separate post-change full frontend pass;
`check_frontend.py` reproduces it. Categories distinguish accepted/rejected
agreement from B128 immediate representation, >64-bit integer source, auxiliary
policy, reversed cache-modifier order, and old-target assembler availability
limits. The raw pre-implementation ptxas matrices remain separate.

## Final implementation verification

The approved implementation changes only the six ordinary synchronous scalar
ST source layouts to the existing `RegOrImm` value domain. It retains the
existing converter, register carrier rules and all surrounding instruction gates.
The common current-immediate check additionally enforces its documented scalar
width and floating-origin invariants. Existing half/FP8/packed diagnostics retain
priority. Four older tests were updated to distinguish the newly admitted
scalar ST sources from retained vector/register-only roles and malformed current
floating metadata; no vector or packed producer permission was enlarged.

The final 422-case comparison uses probe SHA-256
`1eb00c8e9a8e0834d699f8ec08838a889fb3cb71d7a87e6a39cbb72544718402`:

| Classification | Cases |
| --- | ---: |
| Both accept | 333 |
| Both reject | 59 |
| Retained B128 immediate representation limit | 10 |
| Retained reversed cache/state modifier order | 8 |
| Unsupported assembler targets, classified before agreement | 6 |
| Retained >64-bit integer-source domain limit | 4 |
| Retained register-only auxiliary cache policy | 2 |
| Unclassified divergences / frontend tool failures | 0 |

All 422 frontend exits were normal success (337) or resolution/check rejection
(85), and all source hashes matched. The six old-target assembler invocations
include two that the frontend also rejects; they are not ST rejection evidence.
Timeouts/launch failures are a separate category in the reproducibility harness.
The initial comparison and pre-diagnostic-repair comparison remain separately
preserved; only the final comparison above pins the final executable.

| Check | Final evidence | Outcome |
| --- | --- | --- |
| Focused immediate + previous ST suite | `cpp-focused-final.log` | 16 passed before the final register-reference assertion extension |
| Repaired five regressions + seven current immediate tests | `cpp-repaired-focused-final.log` | 12 passed |
| Complete resolved IR tests, including final register-reference coverage | `cpp-full-final.log` | 2079 passed; 2 instrumentation-dependent tests skipped; 0 failed |
| Spec, normalization, generated-value and memory-model tests | `python-focused.log` | 132 passed |
| Final generated scalar source API assertions | `python-model-final.log` | 1 passed |
| Installed public cache-hint consumer | `consumer-ctest-final.log` | 1 real CTest passed with `--no-tests=error` |

The two existing skipped tests require explicit test-only linker interception:
`PTX_ATOMIC_VALUE_CHECKER_WRAP` and `PTX_PACKED_SETP_CHECKER_WRAP`; neither
instrumentation mode was enabled in this build. The complete test suite did run
all other registered cases. Earlier logs retain the initial five failing
regressions and their repair history; `cpp-full-final.log` is the final result.
The consumer was rebuilt against the installed final library and reads both
scalar source union arms after parser/AST/source destruction. It preserves
register-only optional cache-policy checks.

The required scoped command sequence uses the recorded existing CMake cache:

```sh
cmake --build /tmp/ptx93-audit-build --target ptx93_probe ptx93_parse_probe test_resolved_ir ptx93_st_tests -j 4
/tmp/ptx93-audit-build/submod/resolved_ir/test_resolved_ir
PYTHONPATH=python/src python -m unittest python.tests.spec.test_ld_st_completeness python.tests.ir.test_resolved_ir_memory python.tests.spec.normalize.test_operands python.tests.code_gen.test_resolved_value_traits
python docs/audits/ptx93_st_immediate/check_frontend.py
cmake --install /tmp/ptx93-audit-build --prefix /tmp/ptx93-st-install
cmake --build /tmp/ptx93-st-installed-consumer -j 4
ctest --test-dir /tmp/ptx93-st-installed-consumer --no-tests=error --output-on-failure
```

The installed consumer configuration uses the installed prefix plus
`/tmp/ptx93-audit-deps/prefix`, explicit Ninja from the tools venv, and
`installed-consumer-hook.cmake`. These are local audit artifacts, not an
environment publication. Parent-owned aggregate evidence separately rechecks
the frozen natural corpus, official samples and earlier focused cases.
