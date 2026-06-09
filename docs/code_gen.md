# PTX Instruction Code Generation

This document describes the instruction code generation flow used by the PTX frontend.

The generator is designed around a clear separation between PTX instruction semantics and C++ backend representation. PTX syntax and legality rules are described by PTX spec YAML files, while C++ code generation details are described by backend mapping YAML files.

## Goals

The code generation system is intended to:

1. keep PTX instruction descriptions declarative;
2. avoid hand-written repetitive C++ IR definitions;
3. generate maintainable C++ code from validated YAML inputs;
4. separate PTX semantics from C++ implementation details;
5. allow each instruction category to evolve independently.

Generated code should be treated as build output. Developers should modify YAML specs, schemas, normalization logic, or templates instead of editing generated C++ files directly.

## High-Level Flow

The generation pipeline is:

```text
PTX spec YAML
    +
C++ backend YAML
    ↓
schema validation
    ↓
normalization
    ↓
CodegenUnit model
    ↓
generator-specific view model
    ↓
Jinja2 template rendering
    ↓
generated C++ headers / sources
```

The important design rule is that raw YAML dictionaries should not be consumed directly by C++ generators. YAML should first be validated and normalized into typed Python models.

## Directory Layout

A typical layout is:

```text
instructions/
  integer_arith.yaml
  floating_point.yaml
  ...

instructions/ptx_cpp_backend_spec/
  integer_arith.yaml
  floating_point.yaml
  ...

schemas/
  ptx-instr-v1.schema.yaml
  ptx-cpp-backend-v1.schema.yaml

python/code_gen/
  model.py
  normalize.py
  cpp/
    gen_ir.py
    templates/
      ptx_ir_instr.gen.hpp.j2
```

The exact paths may change, but the responsibilities should remain the same.

## PTX Spec YAML

PTX spec YAML describes PTX facts.

It answers questions such as:

* what opcode exists;
* which category the opcode belongs to;
* which PTX variants exist;
* which modifiers are required, optional, fixed, or absent;
* which operand patterns are legal;
* which PTX or SM version is required;
* which semantic rule should be used.

Example:

```yaml
schema: ptx-instr/v1
ptx_isa: "9.2"
category: integer_arithmetic
section: "9.7.1"

instructions:
  - opcode: add
    section: "9.7.1.1"
    syntax: "add{.sat}.{type} dst, src1, src2"
    operands: binary_arith

    variants:
      - name: add_integer_no_sat
        modifiers:
          - name: sat
            kind: flag
            presence: absent

          - name: type
            kind: type
            domain: scalar_types
            presence: required
            values:
              - u16
              - u32
              - u64
              - s16
              - s32
              - s64

      - name: add_sat_s32
        modifiers:
          - name: sat
            kind: flag
            presence: fixed
            value: true

          - name: type
            kind: type
            domain: scalar_types
            presence: fixed
            value: s32
```

PTX spec YAML should not contain C++ field names, C++ enum names, C++ include paths, or visitor function names.

## C++ Backend YAML

C++ backend YAML describes how PTX instructions are represented in the current C++ IR.

It answers questions such as:

* which C++ struct represents an instruction;
* which C++ field name represents a PTX modifier;
* which C++ type is used for a field;
* how PTX values map to C++ enum values;
* which headers are required;
* whether modifier fields are emitted directly, inside a nested struct, or inside a nested variant.

Example:

```yaml
schema: ptx-cpp-backend/v1
backend: cpp
target: all
spec_schema: ptx-instr/v1
category: integer_arithmetic

includes:
  - <numeric>
  - <optional>
  - "ptx_ir/base.hpp"
  - "ptx_ir/details.hpp"
  - "ptx_ir/source_loc.hpp"

namespace: ptx_frontend

domains:
  scalar_types:
    cpp_type: ScalarType
    values:
      u16: ScalarType::U16
      u32: ScalarType::U32
      u64: ScalarType::U64
      s16: ScalarType::S16
      s32: ScalarType::S32
      s64: ScalarType::S64
    default: ScalarType::U32

instructions:
  add:
    cpp: InstrIntegerAdd
    emit:
      kind: sub_struct
      instance: data
      type: Data

    modifiers:
      sat:
        field: sat
        cpp_type: bool
        default: "false"

      type:
        field: type_
        cpp_type: ScalarType
        domain: scalar_types

    operands:
      dst:
        field: dst
        cpp_type: WithLoc<Operand>

      src1:
        field: src1
        cpp_type: WithLoc<Operand>

      src2:
        field: src2
        cpp_type: WithLoc<Operand>
```

## Why PTX Spec and Backend YAML Are Separate

For generating a minimal C++ struct, the backend YAML may appear sufficient. However, the frontend must eventually generate or drive more than struct definitions.

The PTX spec YAML is the source of truth for PTX legality and semantics:

```text
add.sat.s32 is legal.
add.sat.u64 is illegal.
add.rn.f32 belongs to floating-point add, not integer add.
src1 may be a register or immediate.
a modifier may be required, optional, fixed, or absent.
```

The backend YAML is the source of truth for C++ representation:

```text
add maps to InstrIntegerAdd.
the PTX type modifier maps to field type_.
u32 maps to ScalarType::U32.
the instruction data member is called data.
```

This separation prevents C++ implementation details from polluting the PTX instruction specification.

## Category-Based Files

Instruction YAML files are organized by category.

For example:

```text
instructions/
  integer_arith.yaml
  floating_point.yaml

instructions/ptx_cpp_backend_spec/
  integer_arith.yaml
  floating_point.yaml
```

Different categories may contain the same PTX mnemonic. For example, both integer arithmetic and floating-point arithmetic may contain an opcode named `add`.

The generated C++ IR does not need to reuse the same C++ instruction type for all PTX mnemonics named `add`.

A recommended mapping is:

```text
integer_arithmetic/add -> InstrIntegerAdd
floating_point/add     -> InstrFloatAdd
```

The parser or registry layer is responsible for dispatching a textual PTX instruction to the correct generated IR type based on opcode and modifiers.

## CodegenUnit

After YAML validation and normalization, generators consume a typed `CodegenUnit`.

A simplified model is:

```python
@dataclass(frozen=True)
class CodegenUnit:
    spec_schema: str
    backend_schema: str
    category: str
    namespace: str
    includes: tuple[str, ...]

    instructions: tuple[InstructionSpec, ...]
    backends: dict[str, InstructionBackend]
    domains: dict[str, DomainBackend]
```

`CodegenUnit` represents one normalized instruction category.

Generators should not read raw YAML dictionaries directly. They should consume `CodegenUnit` and then build generator-specific view models.

## IR Generation

The IR generator is implemented as:

```text
CodegenUnit
    ↓
IrHeaderView
    ↓
ptx_ir_instr.gen.hpp.j2
    ↓
generated C++ header
```

The generator-specific view model is intentionally separate from `CodegenUnit`.

`CodegenUnit` represents normalized semantic and backend data.

`IrHeaderView` represents exactly what the IR header template needs to render.

This keeps templates simple and avoids placing business logic inside Jinja2 templates.

## Emit Kinds

The C++ backend supports several emission strategies.

### direct

`direct` emits modifier fields directly inside the instruction struct.

Backend YAML:

```yaml
emit:
  kind: direct
```

Generated C++:

```cpp
template <OperandLike Operand>
struct InstrIntegerAdd {
    bool sat = false;
    ScalarType type_ = ScalarType::U32;

    WithLoc<Operand> dst;
    WithLoc<Operand> src1;
    WithLoc<Operand> src2;
};
```

This is the simplest representation and is suitable for small instructions.

### sub_struct

`sub_struct` emits modifier fields inside one nested data struct.

Backend YAML:

```yaml
emit:
  kind: sub_struct
  instance: data
  type: Data
```

Generated C++:

```cpp
template <OperandLike Operand>
struct InstrIntegerAdd {
    struct Data {
        bool sat = false;
        ScalarType type_ = ScalarType::U32;
    };

    Data data;

    WithLoc<Operand> dst;
    WithLoc<Operand> src1;
    WithLoc<Operand> src2;
};
```

In this mode, `emit.type` is the nested struct name, not a namespace-level type name.

### sub_variant

`sub_variant` emits multiple nested data structs and a nested `std::variant` alias.

Backend YAML:

```yaml
emit:
  kind: sub_variant
  instance: data
  type: Data
  alternatives:
    - name: IntegerData
```

Generated C++:

```cpp
template <OperandLike Operand>
struct InstrIntegerAdd {
    struct IntegerData {
        bool sat = false;
        ScalarType type_ = ScalarType::U32;
    };

    using Data = std::variant<IntegerData>;

    Data data;

    WithLoc<Operand> dst;
    WithLoc<Operand> src1;
    WithLoc<Operand> src2;
};
```

For `sub_variant`, `emit.type` is the variant alias name.

`alternatives[].name` is the nested struct name used as one alternative of the variant.

The alias name and alternative names must not conflict.

Invalid:

```yaml
emit:
  kind: sub_variant
  instance: data
  type: Data
  alternatives:
    - name: Data
```

This would generate invalid C++:

```cpp
struct Data {};
using Data = std::variant<Data>;
```

## `alternatives` Rules

`alternatives` is only valid when:

```yaml
emit:
  kind: sub_variant
```

It is required for `sub_variant` and must not appear under `direct` or `sub_struct`.

Minimal form:

```yaml
emit:
  kind: sub_variant
  instance: data
  type: Data
  alternatives:
    - name: IntegerData
```

Multiple alternatives:

```yaml
emit:
  kind: sub_variant
  instance: data
  type: Data
  alternatives:
    - name: ScalarData
    - name: PackedData
```

Generated C++:

```cpp
struct ScalarData {
    ...
};

struct PackedData {
    ...
};

using Data = std::variant<
    ScalarData,
    PackedData
>;
```

An alternative may optionally list PTX spec variants covered by that alternative:

```yaml
emit:
  kind: sub_variant
  instance: data
  type: Data
  alternatives:
    - name: ScalarData
      variants:
        - add_integer_no_sat
        - add_sat_s32

    - name: PackedData
      variants:
        - add_simd_no_sat_sm90
        - add_packed_optional_sat_sm120
```

The `variants` field refers to PTX spec variant names, not C++ variant types.

Recommended semantic rules:

1. If there is exactly one alternative, `variants` may be omitted. The alternative covers all PTX variants of the instruction.
2. If there are multiple alternatives, every alternative should list `variants`.
3. The union of all listed variants should cover all PTX spec variants of the instruction.
4. A PTX spec variant should not appear in more than one alternative.
5. Every listed PTX spec variant must exist in the corresponding PTX spec YAML.

Rules 3 to 5 should be checked in Python normalization because they require cross-checking backend YAML against PTX spec YAML.

## Nested Data Types

The generator intentionally emits data structs inside instruction structs.

This avoids namespace-level detail type conflicts.

For example, both `add` and `sub` can use a nested struct named `Data` safely:

```cpp
template <OperandLike Operand>
struct InstrIntegerAdd {
    struct Data {
        bool sat = false;
        ScalarType type_ = ScalarType::U32;
    };

    Data data;
};

template <OperandLike Operand>
struct InstrIntegerSub {
    struct Data {
        bool sat = false;
        ScalarType type_ = ScalarType::U32;
    };

    Data data;
};
```

The fully qualified types are distinct:

```cpp
InstrIntegerAdd<Operand>::Data
InstrIntegerSub<Operand>::Data
```

This avoids duplicate namespace-level definitions such as:

```cpp
struct IntegerArithData {};
struct IntegerArithData {}; // invalid
```

Generated checker, printer, visitor, or lowering code should refer to nested alternative types with the instruction scope when needed.

For example:

```cpp
std::get<InstrFoo<Operand>::SomeData>(instr.data)
```

not:

```cpp
std::get<SomeData>(instr.data)
```

## Includes

Backend YAML may write C++ include tokens directly:

```yaml
includes:
  - <numeric>
  - <optional>
  - "ptx_ir/base.hpp"
  - "ptx_ir/details.hpp"
  - "ptx_ir/source_loc.hpp"
```

The normalization layer is responsible for converting these entries into valid C++ `#include` lines.

Generated output:

```cpp
#include <numeric>
#include <optional>
#include "ptx_ir/base.hpp"
#include "ptx_ir/details.hpp"
#include "ptx_ir/source_loc.hpp"
```

## Field Ordering

Generated field order should be deterministic.

The recommended ordering is:

1. modifier fields in the order declared by backend YAML;
2. any remaining fields discovered from PTX spec variants;
3. operands in the normalized operand order.

Stable field ordering improves generated diff readability and avoids unnecessary rebuild churn.

## Schema Responsibilities

JSON/YAML schemas should check local structural rules.

Examples:

* `emit.kind` must be one of `direct`, `sub_struct`, `sub_variant`, or `custom`;
* `sub_struct` requires `instance` and `type`;
* `sub_variant` requires `instance`, `type`, and `alternatives`;
* `alternatives` is only valid under `sub_variant`;
* each alternative must have a `name`.

Schemas should not be expected to check cross-file semantic rules.

Cross-file and cross-field semantic rules should be checked in Python normalization.

Examples:

* a backend instruction must reference an existing PTX instruction;
* `alternatives[].variants` must reference existing PTX spec variants;
* multiple alternatives must cover all PTX variants without overlap;
* `sub_variant.emit.type` must not equal any `alternatives[].name`.

## Normalization Responsibilities

The normalization layer should:

1. load raw PTX spec YAML and backend YAML;
2. validate them against schemas;
3. expand references and operand patterns;
4. normalize modifiers, operands, variants, domains, and backend mappings;
5. build a typed `CodegenUnit`;
6. perform semantic checks that require both PTX spec and backend YAML.

The generator should receive only normalized, typed models.

## Template Responsibilities

Jinja2 templates should only handle formatting.

Templates may contain:

* loops;
* simple conditionals;
* field substitution;
* whitespace control.

Templates should not contain semantic decisions such as:

* resolving modifier domains;
* determining C++ default values;
* validating `sub_variant` alternatives;
* deciding whether a PTX variant is legal.

That logic belongs in normalization or generator Python code.

## Generated Code Policy

Generated files should contain a warning header:

```cpp
// Auto-generated. Do not edit manually.
```

Developers should not manually edit generated C++ files.

To change generated output, modify one of:

* PTX spec YAML;
* C++ backend YAML;
* schema files;
* normalization logic;
* generator view-model construction;
* Jinja2 templates.

## Recommended Development Workflow

When adding or changing an instruction:

1. Update the PTX spec YAML for the relevant category.
2. Update the C++ backend YAML for the same category.
3. Run schema validation.
4. Run normalization.
5. Generate C++ output.
6. Build the minimal compile demo.
7. Add or update tests for generated output.
8. Only then connect the instruction to parser, checker, printer, or visitor generation.

## Design Summary

The current IR generation design uses:

```text
category-based YAML files
typed CodegenUnit models
instruction-local nested data structs
Jinja2 templates
schema validation
Python semantic normalization
```

The most important rules are:

1. PTX spec YAML describes PTX legality and semantics.
2. C++ backend YAML describes C++ representation.
3. `sub_struct.emit.type` names a nested struct.
4. `sub_variant.emit.type` names a nested `std::variant` alias.
5. `sub_variant.alternatives[].name` names nested alternative structs.
6. Generated data structs are nested inside instruction structs.
7. Raw YAML should not be consumed directly by C++ generators.
8. Generated C++ files should never be edited manually.
