# YAML Instruction Spec: Structure and Principles

Current source forms, type/target restrictions and retained limitations are maintained in the [SDK support contract](../sdk_support.md#yaml-instruction-spec). This page retains technical reference material and evidence.

## Purpose

YAML files under `python/src/ptx_frontend/spec/resources/ptx_spec/` are the canonical
declarative source of PTX
instruction facts. They describe legal source forms, variants, operand layouts,
availability, and rule identifiers. The Python generator derives Syntax,
Resolved, and checker descriptors plus C++ instruction structures from them.
They are neither C++ templates nor backend-layout configuration.

Every file uses the packaged sibling schema
`python/src/ptx_frontend/spec/resources/ptx-instr-v1.schema.yaml` (and its local comment
references `../ptx-instr-v1.schema.yaml`). `instructions/ptx_spec/` is the repository input directory used by the
source build:

```yaml
schema: ptx-instr/v1
ptx_isa: "9.3"
category: arithmetic
codegen_category: arithmetic
```

The schema validates field shape and primitive enums. The normalizer enforces
cross-field generator invariants, for example that a variant cannot declare
both `operands` and `operand_layouts`. Neither a schema enum, a YAML entry, nor
a `rule` identifier is an executable checker by itself: it is useful only when
the normalizer, generated descriptors, resolver, and checker already implement
the corresponding contract.

## Extension boundary

[SDK support: Extension boundary](../sdk_support.md#yaml-instruction-spec--extension-boundary).

The canonical YAML/database is the only machine-readable source for an
instruction form. Do not create a parallel handwritten opcode/variant registry
or capability API.

A YAML-only addition is appropriate only when the form reuses implemented
syntax and operand primitives, modifier domains, type expressions, and
descriptor-backed constraints. It still needs a non-overlapping variant, the
normalizer/database checks, generated descriptors, resolver/checker coverage,
and source/target availability facts. Reusing a `rule` name is not evidence
that a new relation will be checked; confirm that its selected descriptor fields
and constraints reach executable checker code.

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
name collisions, ambiguous modifier-slot bindings, and variants whose accepted
ordered modifier sequences overlap (including declared order aliases).

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
`result_type` in mixed Add. Matching follows the `modifiers` declaration order;
optional and absent slots may be skipped, but required slots may not. Repeated
spellings are allowed only between required/fixed slots when their ordered
positions disambiguate the binding. Distinct variants must accept disjoint
ordered modifier sequences; the database rejects overlap before emission.

Use the pinned PTX manual's **Syntax** order as the canonical declaration order.
When compatibility evidence supports another spelling, a variant may declare
`modifier_order_aliases`: each item is a complete permutation of its modifier
slot names, including absent slots. For example, mixed Add/Sub use canonical
`rounding, sat, result_type, input_type, ftz` and preserve the historical order:

```yaml
modifier_order_aliases:
  - [rounding, result_type, input_type, ftz, sat]
```

Aliases select the same variant and bind the same semantic fields, defaults,
and source locations; they do not permit arbitrary reordering or relax checker
constraints. The database rejects incomplete permutations and aliases that
give the same source sequence different slot bindings.

This canonical-order policy resolves an inconsistency in the pinned
[mixed-precision Sub documentation](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub):
Syntax places `.sat` before the types, while an example places it after them.
Both forms are retained; Syntax is not assumed to enumerate every spelling
accepted by an assembler.

Consumers generating code from the Python package should upgrade it together
with the C++ frontend and regenerate/rebuild their artifacts. Stable variant
IDs and field names do not imply binary-layout compatibility: canonical slot
reordering also changes generated member order, and syntax descriptors now
carry an additional alias span.

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


A historical omitted floating-control variant may declare
`floating_point_default: approximate_flush_to_zero`, `approximate`, or
`round_nearest_even`. This closed typed interpretation is separate from source
modifiers: every declared `rounding`, `approx`, `full`, and `ftz` slot must have
`presence: absent`. The first interpretation requires fixed `type: f32`; the
other two require fixed `type: f64`. Unknown values or contradictory explicit
controls/types are rejected. Normalization and Resolved IR preserve this fact;
the generated exact class exposes static `FloatingPointDefault
floating_point_default`, without synthesizing source modifier fields. See the
[historical contract](resolved_ir_design.md#historical-omitted-floating-controls).

## Operands and operand patterns

[SDK support: Operands and operand patterns](../sdk_support.md#yaml-instruction-spec--operands-and-operand-patterns).

An operand has a stable `name`, syntax `kind`, semantic `role`, `access`, and
optional `type`:

```yaml
- name: src1
  kind: reg_or_imm
  role: src1
  access: read
  type: {expr: modifier(type)}
```

Every generated operand payload also needs an explicit module-reference policy.
`code_gen.reference_policy` classifies each semantic payload in the public
`REFERENCE_VALUE_KINDS` or `REFERENCE_FREE_VALUE_KINDS`; an unclassified type
fails context construction before generation begins. Reference-bearing payloads are the resolved primitives that
can carry bound declaration/symbol identity (for example register, predicate,
symbol, address, vector, call/control, and tensor-coordinate forms). The
generated `visit_instruction_references` visitor exposes them to module and
AST-free revalidation. Reference-free payloads are values without such identity
(for example modifier enums, immediates, and special-register references) and
are intentionally not visited. Choose this classification from the actual
resolved payload and validation requirement, not from an operand's YAML name.
The C++ consumer must then be extended in `ptx_module_availability.cpp`:
`ReferenceBearingOperandPayload` admits the payload, `collect_operand_references`
recursively projects it to `ModuleReferenceUse`, and `check_module_references`
performs the owned-symbol, kind, scope, register-state, and parameterized-index
checks. A generated visitor alone is therefore insufficient; the collector's
exhaustive branch deliberately fails compilation for a newly admitted payload
without its owned-module validation.

`immediate_conversion` is a separate use contract for integer immediates. It
defaults to `narrow`, which retains the low bits of the resolved scalar width
after decoding the 64-bit source. Use `require_target_range` only where a
semantic operand must be representable at that width:

```yaml
- name: barrier
  kind: reg_or_imm
  role: barrier
  access: read
  type: u32
  immediate_conversion: require_target_range
```

It is independent of `type`: fixed scalar types and modifier-derived types may
each use either conversion. Bounded control operands should instead reuse a
generated range, exact-value, or multiple-of constraint when that fully states
their rule; those constraints compare the original decoded source bits.

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

`parameter` is valid only on `kind: addr`. It normally accompanies a state-space
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

A `reg_vector` operand must declare legal element counts through
`vector.arity`. Static forms use an integer or list:

Legacy memory vectors instead link arity to the required runtime vector
modifier:

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

## Immediate operand constraints

Variant-level `constraints` can impose executable integer rules on a named
operand. Depending on the constraint kind, it may be `kind: imm` or
`kind: reg_or_imm`; a `reg_or_imm` rule applies only when the source is a known
immediate:

```yaml
constraints:
  - {kind: immediate_value, operand: mode, values: [4, 8, 16]}
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

`immediate_value` is one non-empty, duplicate-free allowlist per variant.
`immediate_range` may occur once per operand and has an inclusive `minimum`
and an optional inclusive `maximum`; omitting `maximum` means no upper bound.
`immediate_multiple_of` is one divisor rule per variant. Like
`immediate_range`, it may name an `imm` or `reg_or_imm` operand: the generated
checker evaluates known immediates and leaves dynamically unknown register
values to runtime. These descriptors may be combined when their named operands
make that meaningful.

All configured values use the generated `uint64_t` domain: an actual YAML
integer in `0..18446744073709551615` (`2^64 - 1`). Negative values, Boolean
values, floats/other non-integers, and values above that limit are rejected.
The normalizer validates every `immediate_value.values[index]` before duplicate
checking, validates `minimum`, present `maximum`, and `divisor` with the same
rule, rejects `maximum < minimum`, and requires `divisor > 0`.

The operand reference is deliberately variant-wide, not layout-local. It must
occur in at least one operand layout. For all three constraint kinds, omission
means that the constraint does not apply in that layout and does not produce a
missing-field error. Where the operand occurs, `immediate_value` requires
`kind: imm`, while `immediate_range` and `immediate_multiple_of` accept
`kind: imm` or `kind: reg_or_imm`. A `reg` occurrence is a normalization error
that identifies the variant, constraint kind, operand, and layout. This permits
optional operands while preserving immediate-only rules where their value
contract requires one.

Both `setmaxnreg.inc.sync.aligned.u32` and
`setmaxnreg.dec.sync.aligned.u32` use the same range and divisibility rules:

```yaml
operands:
  - {name: count, kind: imm, role: src, access: read, type: u32}
constraints:
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

Thus `192` is valid while `23`, `257`, and `25` are rejected for either action. `bfe.u32` and
`bfi.b32` each use two independent inclusive ranges, `offset` and `width`,
both `0..255`; both operands are immediate operands in their only layout.

At resolution time an integer immediate carries its scalar type, use-width
bits, evaluated 64-bit source bits, and numerical signed-negativity. The
source is signed unless it has `U`/`u` or exceeds `INT64_MAX`; unary minus
preserves that type and wraps unsigned values. Range, multiple-of, and
exact-value checks require a nonnegative source and compare original source
bits, so a nonzero value that narrows to an allowed target-width bit pattern
cannot satisfy a fixed control rule. A signed `-0` therefore behaves as zero.
Floating immediates resolve to IEEE raw bits: decimal
forms require `f32` or `f64`, and `0f...`/`0d...` are unsigned 32-/64-bit
bit-pattern literals that require exactly `f32`/`f64` and cannot have a sign.
Consequently these constraints are integer-domain rules; do not use their
numeric-looking bounds to express floating-point ordering.

Normalization diagnostics name the variant, constraint kind, precise field
(including `values[index]`), and illegal value for malformed configuration.
At runtime a bad value/range/divisibility result is an
`ImmediateValueMismatch` anchored at the immediate operand; an impossible
generated descriptor that references a missing/non-immediate operand is a
`RuleViolation` anchored at the instruction. This makes configuration errors
and source-program errors distinguishable.

## Complete example: bar

```yaml
operand_patterns:
  bar_sync_immediate_barrier:
    - {name: barrier, kind: imm, role: barrier,
       access: read, type: u32, immediate_conversion: require_target_range}
  bar_sync_barrier:
    - {name: barrier, kind: reg_or_imm, role: barrier,
       access: read, type: u32, immediate_conversion: require_target_range}

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

[SDK support: Availability, rules, and examples](../sdk_support.md#yaml-instruction-spec--availability-rules-and-examples).

Every variant must declare:

```yaml
availability:
  ptx: "8.0"
  sm: 100
  family: sm_100f      # optional minimum family-specific source-feature target
rule: integer_arith.add # optional but recommended
```

An `any_of` clause may use `targets: [sm_32, sm_35, sm_37]` for an exact
architecture/flavor membership set. `targets` must be a nonempty list of unique
canonical `sm_<uint32>[a|f]` spellings and is mutually exclusive with `target`.
Membership requires an explicit target identity and is conjunctive with PTX, SM,
family, capabilities, and source-option requirements; it never infers neighboring
architectures or flavors. Generated `AvailabilityTarget` arrays have process
lifetime and are borrowed by `AvailabilityClause::exact_targets`. The DNF limit
remains six clauses; the target list has no fixed descriptor capacity. Manual
consumers supplying both singular and set fields must satisfy both. Scalar member
ordering changed to retain descriptor size; consumers must rebuild together.

An `any_of` clause may require the source programmer option with
`map_f64_to_f32: true`, for example
`{ptx: "1.0", target: sm_10, map_f64_to_f32: true}`. This is an AND-term alongside
PTX, exact target (including flavor), SM, family, and capabilities. If present,
the value must be Boolean true; false, numeric/string substitutes, and placement
in the legacy availability object are invalid. Omission adds no option requirement.
The emitter sets `checker::AvailabilityClause::requires_map_f64_to_f32`; common
availability evaluation checks `checker::TargetInfo::map_f64_to_f32`, whose
default is false. This source control is not a hardware capability and never
changes the type or width of a resolved operand.

An operand with `kind: reg_vector` generates a `ResolvedRegisterVector` payload.
Static `vector.arity` values are descriptor checks only; dynamic
`vector.arity: {expr: modifier(vector)}` also records a link to the selected
runtime modifier field. This data does not participate in modifier-variant
selection. For example, scalar, pack, and unpack `mov` are three layouts of one
modifier variant rather than copied variants with overlapping `.b16/.b32/.b64`
forms, while `ld.v2` and `ld.v4` are one vector variant selected by a required
runtime `vector` modifier.

## Current Add coverage

[SDK support: Current Add coverage](../sdk_support.md#yaml-instruction-spec--current-add-coverage).

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

## Whole-opcode acceptance

Do not call an opcode complete merely because one new variant parses, a YAML
database test passes, or the generated model has a nonzero variant count. Freeze
the claimed PTX 9.3 scope against the relevant normative section, including
adjacent excluded forms, then use independently derived positive and negative
source cases for its modifier, operand, availability, and diagnostic
boundaries. Model/generator consistency checks show that YAML was normalized
and emitted as intended; they are not independently derived ISA-conformance
evidence.

Where declaration type, binding identity, call/control metadata, function
context, or address provenance affects legality, test through a declaration-aware
module with source version and target context. A standalone instruction test is
useful for local syntax and resolution, but cannot replace that module check.
Mutate the public Resolved IR and run AST-free validation for retained
identities, provenance, and descriptor invariants. Finally, compile and run an
installed-package consumer using only public headers. These frontend checks do
not prove simulator execution, dynamic protocol correctness, assembler
acceptance beyond the independent probes, or GPU/hardware behavior.

Recommended validation:

```bash
PYTHONPATH=python python3 -m unittest discover -s python/tests -t python -p 'test_*.py' -v
cmake --build out/build/ci-linux-gcc-debug -j2
ctest --preset ci-linux-gcc-debug --output-on-failure
```

## Typed video contracts

[SDK support: Typed video contracts](../sdk_support.md#yaml-instruction-spec--typed-video-contracts).

## Exclusive numeric SM availability bound

[SDK support: Exclusive numeric SM availability bound](../sdk_support.md#yaml-instruction-spec--exclusive-numeric-sm-availability-bound).

## Historical 101 source profiles and validation features

[SDK support: Historical 101 source profiles and validation features](../sdk_support.md#yaml-instruction-spec--historical-101-source-profiles-and-validation-features).
