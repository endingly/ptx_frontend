# YAML Instruction Spec: Structure and Principles

## Purpose

YAML files under `instructions/ptx_spec/` are the declarative source of PTX
instruction facts. They describe legal source forms, variants, operand layouts,
availability, and rule identifiers. The Python generator derives Syntax,
Resolved, and checker descriptors plus C++ instruction structures from them.
They are neither C++ templates nor backend-layout configuration.

Every file uses `instructions/schemas/ptx-instr-v1.schema.yaml`:

```yaml
schema: ptx-instr/v1
ptx_isa: "9.3"
category: arithmetic
codegen_category: arithmetic
```

The schema validates field shape and primitive enums. The normalizer enforces
cross-field generator invariants, for example that a variant cannot declare
both `operands` and `operand_layouts`.

## Predefined-data references

`$name` is the sole DSL spelling for a reference to predefined data in the
current YAML file, and it needs no quotes. `type_sets` and `value_sets` are
referenced as `$name` in `values`; `operand_patterns` are likewise referenced as `$name` in
`operands`. A bare string is no longer a reference. The normalizer recursively
expands references, detects cycles, and guarantees that a reference and its
inline expansion produce identical `InstructionSpec` data. `$` is not used by
type expressions, which have their own function syntax to avoid this ambiguity.

## Top-level structure

Common top-level fields are:

| Field | Meaning |
| --- | --- |
| `type_sets` | modifier-value sets referenced as `$name` |
| `value_sets` | non-type modifier-value sets referenced as `$name` |
| `operand_patterns` | reusable named operand lists |
| `category` | PTX documentation category represented by this YAML file |
| `codegen_category` | generated-source partition for this file's instructions |
| `instructions` | one or more opcode definitions |

An instruction requires `opcode` and `variants`; it may also have instruction-
level `syntax`, `operands`, `section`, and `doc`. Both `category` and
`codegen_category` are required file-level fields and must not be repeated on
an instruction. An opcode may naturally occur in several category YAML files,
but all its definitions must use the same `codegen_category`. One YAML may
also split one opcode into several instruction definitions when its variants
belong to distinct PTX document sections. `arithmetic.yaml` is the deliberate
exception that combines PTX 9.7.1 through 9.7.5 under one category.

Merging follows sorted spec-path and in-file declaration order. The file path is
already the definition source, so no separate `fragment` ID is needed. Before
emission, the database rejects duplicate variant IDs, PascalCase C++ variant
name collisions, a spelling owned by multiple active modifier slots within one
variant, and variants whose accepted unordered modifier sets overlap.

## Variants and modifiers

A variant `name` is a stable machine-readable identifier. It generates the C++
variant name and descriptor key. In the current model, a variant represents a
mutually exclusive set of modifier slots/presence and value constraints. It is
not determined by operand count or a target-version interval. Forms that differ
only because later targets add allowed values belong to one variant.

Core modifier fields are:

```yaml
- name: type
  kind: type
  domain: scalar_types
  presence: required
  values: [$add_integer_scalar]
```

`presence` means:

| Value | Meaning |
| --- | --- |
| `absent` | the modifier kind must not occur |
| `optional` | it may be omitted; if present it must match an allowed spelling |
| `required` | it must occur and match one of `values` |
| `fixed` | it must occur with one fixed value; the resolved struct emits a static constexpr member |

An `optional` modifier must explicitly define its semantic `default` when it is
omitted. The default type must agree with the modifier kind: `flag` uses a
Boolean, `type` uses one scalar type from `values`, `rounding` uses a
rounding-mode value such as `rn`, and legacy `cache` uses the semantic
source-absence sentinel `unspecified`. For example:

```yaml
- name: sat
  kind: flag
  presence: optional
  token: .sat
  default: false

- name: type
  kind: type
  presence: optional
  values: [u32, u64]
  default: u32

- name: rounding
  kind: rounding
  domain: rounding_modes
  presence: optional
  values: [$rounding_modes]
  default: rn

- name: cache
  kind: cache
  domain: cache_operators
  presence: optional
  values:
    - value: [ca, cg, cs, lu, cv]
      availability: {ptx: "2.0", sm: 20}
  default: unspecified
```

When the modifier is omitted, the resolver stores this default in the resolved
field with empty `locs`. An explicit modifier overrides it and retains its
source location. `absent`, `required`, and `fixed` modifiers must not define
`default`. A default remains an active semantic value, so the checker still
applies its value availability. Because no modifier source range exists, such
a diagnostic falls back to the whole instruction range. Legacy `cache` is the
exception: `unspecified` is a source-absence sentinel rather than a spellable
PTX value, so it never triggers modifier-value availability.

`constraints` may carry the typed `memory_consistency` descriptor. It names
the generated semantics, scope, cache, and address fields (and optionally
mmio/state-space fields); normalization rejects inactive or
unknown references. This keeps memory qualifiers independent in syntax while
the descriptor-backed checker owns their cross rules. `omitted` and `none` are
source-absence defaults, not spellable modifier values.

`flag` normally provides `token: ".sat"`; a type token is normally derived
from `value` or `values`. `name` is the modifier-slot ID local to one variant,
while `kind` determines the resolved value type. The same spelling may bind a
different slot in another variant: `.f32` binds `type` in standard Add and
`result_type` in mixed Add. Within one variant, each spelling must have one
unique active owner. Matching ignores source order, and distinct variants must
accept disjoint unordered modifier sets; the database rejects overlap before
emission.

One `values` item may be an object to add target availability for a semantic
value:

```yaml
values:
  - u32
  - value: u64
    availability: {ptx: "2.0", sm: 20}
```

A value without availability adds no requirement beyond its variant. When
`u64` is selected, the checker additionally verifies its PTX, SM, and family
requirements after variant/layout checks and anchors a diagnostic at that
modifier. Value availability can only add requirements; it cannot lower the
variant availability.

When later PTX versions add values to an existing modifier form, set the
variant availability to the common baseline and attach the additional
requirements to those values. For example, `add.u32` and `add.u16x2` share the
no-sat variant, while the latter value requires PTX 8.0 / sm_90. Do not create
an `add_simd_no_sat_sm90` variant solely for that version difference.

## Operands and operand patterns

An operand has a stable `name`, syntax `kind`, semantic `role`, `access`, and
optional `type`:

```yaml
- name: src1
  kind: reg_or_imm
  role: src1
  access: read
  type: {expr: modifier(type)}
```

The full generated resolve path currently supports `reg`, `imm`, `reg_or_imm`,
`pred`, `pred_or_not`, `addr`, and `reg_vector`. `pred_or_not` accepts `%pN`
or `!%pN` and preserves the complement bit in resolved IR. The schema can
describe many more PTX kinds. Adding a schema enum does not implement it:
Python Syntax/Resolved models, the C++ resolver, and the checker must all be
extended. `type` may reference a modifier (`modifier(type)`) or name a fixed
scalar type such as `u32`. The only supported type-expression function today is
`modifier(name)`, which reads an active `kind: type` modifier of the current
variant. The schema retains `same_as(...)`, `one_of(...)`, and
`same_size_as(...)` as future syntax, but the normalizer explicitly rejects
them as unsupported.

An address operand may similarly derive its required state space from an
active `kind: state_space` modifier:

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space: {expr: modifier(state_space)}
```

The normalizer preserves the reference and the resolved descriptor stores the
modifier field ID. The checker compares it only when the address has a known
declaration-derived effective state space; register, immediate, and standalone
addresses remain unknown rather than being inferred from spelling.
The current scalar/vector `ld/st` explicit forms use one runtime modifier field per
opcode rather than duplicating a variant for each state-space value.

An explicit `.param` address may add a narrow direction and function-context
constraint:

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space: {expr: modifier(state_space)}
  parameter:
    direction: input
    function_availability: {ptx: "2.0", sm: 20}
```

`parameter` is valid only on `kind: addr`. It must accompany a state-space
modifier expression whose active modifier permits (or is fixed to) `param`;
the normalizer rejects a detached or ineffective constraint. The resolved
descriptor stores a typed input/return direction and availability. When the
selected runtime state space is `.param`, the common checker first rejects a
known wrong parameter direction. Otherwise it applies the function
availability for a return constraint or a device-function address. Thus the
current load constraint lets an entry input parameter use the explicit-form
baseline but requires PTX 2.0 / SM 20 in a device function, while the store
return constraint requires that target in every context. Unknown identity is
not assigned a direction, and a known non-`.param` symbol is handled only by
the ordinary exact state-space mismatch.

A scalar string or list defines a static effective-address allowlist. List
items may be plain state-space strings or `value`/`availability` objects:

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space:
    - value: const
      availability: {ptx: "3.1"}
    - global
    - local
    - shared
```

The scalar form is equivalent to a one-entry list without an additional target
requirement. Static values and `expr: modifier(...)` are mutually exclusive.
The resolved descriptor maps every static value to `MemoryStateSpace` and keeps
its availability. The common checker rejects a known effective space outside
the list and checks availability on the matching entry; an unknown register,
immediate, or standalone address remains accepted. Current generic scalar/vector
loads use the example policy above, while generic scalar/vector stores allow only
`.global/.local/.shared`.

The current scalar/vector `ld/st` variants reuse one type set containing
`.b8/.b16/.b32/.b64`, `.u8/.u16/.u32/.u64`, `.s8/.s16/.s32/.s64`, and `.f32`,
then append `.f64` at the variant. Legacy loads additionally model
`.ca/.cg/.cs/.lu/.cv`, legacy stores model `.wb/.cg/.cs/.wt`, and all explicit
cache spellings attach PTX 2.0 / SM 20 availability while omission resolves to the
non-spellable `unspecified` sentinel. PTX's effective hardware defaults still
follow the ISA (`ld` behaves as `.ca`, `st` behaves as `.wb` when the modifier
is omitted), but Resolved IR intentionally preserves `Unspecified` so source
provenance and modifier-value availability stay distinguishable. Explicit `.f64` attaches SM 13
availability; generic `.f64` does not duplicate it because the generic variant
already requires SM 20. Data operands use `type: {expr: modifier(type)}`, so
the runtime modifier and its location drive the common fundamental-type check.
Legacy memory-vector payloads are capped at 128 bits: `.v2` accepts modeled
types through 64 bits and `.v4` through 32 bits. A generated `memory_vector`
constraint additionally permits only 256-bit `.v8` × 32-bit and `.v4` × 64-bit
forms at PTX 8.8/SM 100, with global space when known.
A register operand may also select an explicit width policy:

```yaml
- name: dst
  kind: reg
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
```

`register_width` defaults to `same_width`. The normalizer rejects the non-default
`equal_or_wider` value on a non-register operand or an operand without a type
expression, preventing a silent no-op. `reg_vector` operands may also use this
policy, applying it to each vector element. The resolved operand descriptor
stores the policy; no runtime Resolved IR field is generated. Current scalar
`ld` destinations, scalar `st` sources, and legacy `.v2/.v4` memory vector
elements use `equal_or_wider`: the declared register size must be at least the
instruction size, after which either-side bit types and signed/unsigned integer
pairs are compatible, floats require exact type/size, and integer/float pairs
remain incompatible. Immediate and special-register checks stay same-width.
Wider actual registers are currently limited to 64 bits; `.b128` remains
rejected until declaration-type target availability is checked. Scalar `.b128`
instruction types remain outside this set.

A `reg_vector` operand must declare legal element counts through
`vector.arity`. Static forms use an integer or list:

```yaml
vector: {arity: [2, 4], type_policy: aggregate, allow_sink: true}
```
For memory vectors, the generated cross rule permits partial sinks only for an
exact 256-bit modern payload; legacy vectors and all-sink forms remain invalid.

Legacy memory vectors instead link arity to the required runtime vector
modifier:

```yaml
- name: dst
  kind: reg_vector
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
  vector: {arity: {expr: modifier(vector)}, type_policy: element}
```

`type_policy: aggregate` checks a vector payload against the whole instruction
bit width and is used by `mov` pack/unpack. `type_policy: element` checks each
element against the instruction type and is used by legacy memory vectors.
Those memory vectors retain their 128-bit legacy forms (`.v2` through 64-bit
types and `.v4` through 32-bit types) and add only the PTX 8.8 256-bit forms
described above.
`VectorArity` is a required modifier domain; it has no optional/default form.

Use semantic roles such as `dst`, `src1`, `barrier`, and `thread_count` rather
than inventing `srcN` names merely for reuse. Role and access are carried into
the resolved descriptor for checking and future rules.

## Operand layouts

`operands` is shorthand for one layout; normalization converts it to a layout
named `default`. Use explicit `operand_layouts` when one modifier variant has
several legal operand shapes:

```yaml
operand_layouts:
  - name: barrier
    operands: $bar_sync_barrier
  - name: barrier_and_thread_count
    operands: $bar_sync_barrier_and_thread_count
```

A layout name is a stable semantic ID, not a C++ layout directive. Declaration
order defines its `ResolvedOperandLayoutTag` index, so renaming or reordering is
a generated ABI change. Only positional `Flat` layouts are supported today, and
the layouts of one variant must be uniquely selected by operand arity/shape.

Do not invent variants for layout differences: `bar.sync a` and
`bar.sync a, b` are two layouts in one `.sync` variant. `bar.sync` and
`bar.cta.sync` have different modifier combinations and are separate variants.

## Complete example: bar

```yaml
operand_patterns:
  bar_sync_immediate_barrier:
    - {name: barrier, kind: imm, role: barrier,
       access: read, type: u32}
  bar_sync_barrier:
    - {name: barrier, kind: reg_or_imm, role: barrier,
       access: read, type: u32}

instructions:
  - opcode: bar
    syntax: "bar{.cta}.sync barrier{, thread_count}"
    variants:
      - name: bar_sync
        availability: {ptx: "1.0", sm: 10}
        modifiers:
          - {name: cta, kind: flag, presence: absent, token: ".cta"}
          - {name: sync, kind: flag, presence: fixed, value: true,
             token: ".sync"}
        operand_layouts:
          - {name: immediate_barrier, operands: $bar_sync_immediate_barrier}
          - {name: barrier, operands: $bar_sync_barrier,
             availability: {ptx: "2.0", sm: 20}}
          - {name: barrier_and_thread_count,
             operands: $bar_sync_barrier_and_thread_count,
             availability: {ptx: "2.0", sm: 20}}
        rule: parallel_sync_and_communication.bar_sync
```

`bar_cta_sync` uses fixed `.cta` and PTX 7.8 availability. A layout may add an
optional `availability`; when absent it adds no requirement beyond the variant.
The checker verifies both the selected variant and selected layout. Thus the
immediate layout is usable at PTX 1.0 / sm_10, while register and thread-count
layouts additionally require PTX 2.0 / sm_20.

## Availability, rules, and examples

Every variant must declare:

```yaml
availability:
  ptx: "8.0"
  sm: 100
  family: sm_100f      # optional compatible f-feature family
rule: integer_arith.add # optional but recommended
```

Common checker logic interprets minimum PTX, SM, and legacy compatible
`f`-feature-family requirements. Family membership is published only by the
explicit target-profile catalog: `sm_100f`, `sm_100a`, and the explicit
`sm_103`/`sm_103f`/`sm_103a` successors publish `sm_100f`, while `sm_120f`
publishes only `sm_120f`. Compatibility is never inferred from an SM number or
target suffix. An `a` target is an exact identity, not a family spelling: use
`any_of: [{target: sm_100a}]` when that exact target is required. Capability
clauses remain independent of both exact targets and families. `rule` is a
stable rule ID for instruction-specific checking. `examples`, `doc`, and
`description` document intent; they do not replace executable tests.

`operand_layouts[].availability` accumulates with variant availability; it
does not override it, and only the selected layout contributes its constraint.
When more than one layout matches the same syntax, the resolver accepts only a
unique layout with strictly more specific operand shapes. Equal or incomparable
candidates are a YAML modeling error; availability cannot resolve the syntax
ambiguity.

An operand with `kind: reg_vector` generates a `ResolvedRegisterVector` payload.
Static `vector.arity` values are descriptor checks only; dynamic
`vector.arity: {expr: modifier(vector)}` also records a link to the selected
runtime modifier field. This data does not participate in modifier-variant
selection. For example, scalar, pack, and unpack `mov` are three layouts of one
modifier variant rather than copied variants with overlapping `.b16/.b32/.b64`
forms, while `ld.v2` and `ld.v4` are one vector variant selected by a required
runtime `vector` modifier.

## Current Add coverage

The section-specific `add` definitions in `arithmetic.yaml` merge automatically.
They cover the standard `.f32/.f32x2/.f64` forms,
mixed-precision `.f32.{f16,bf16}`, and the half-precision
`.f16/.f16x2/.bf16/.bf16x2` forms. Rounding resolves to `RoundingMode`, and
value availability expresses the `sm_20` requirement of `.rm/.rp.f32`.
Packed and half/bfloat forms accept register operands as required by PTX.

The mixed variant has two named slots: `result_type` is fixed to `f32`, while
`input_type` accepts `f16/bf16`. Structured operand type expressions refer to
those slots directly, so neither resolver nor checker re-infers operand types
from modifier position or text. The variant requires PTX 8.6 / sm_100 and
supports optional rounding and `.sat`.

## Authoring principles

1. Record PTX semantic facts, not generation preferences; never use C++ storage
   options such as `direct`, `sub_struct`, or `sub_variant`.
2. Use stable descriptive variant names, for example `add_sat` and
   `bar_cta_sync`; do not encode a version suffix that belongs to allowed-value
   availability.
3. Reuse `type_sets`, `value_sets`, and `operand_patterns`, but do not force semantically
   different operands into one pattern.
4. Confirm that lexer/AST can form the needed operand shape before adding a
   spec; otherwise extend the syntax layer first.
5. A new layout needs resolver/checker tests, especially tag bounds and a
   tag/payload mismatch.
6. Passing the schema does not prove generator support; run Python tests,
   CMake build, and CTest.

Recommended validation:

```bash
PYTHONPATH=python python3 -m unittest discover -s python/tests -t python -p 'test_*.py' -v
cmake --build out/build/ci-linux-gcc-debug -j2
ctest --preset ci-linux-gcc-debug --output-on-failure
```
