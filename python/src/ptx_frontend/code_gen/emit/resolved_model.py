"""Emit direct resolved IR classes from the canonical lowering."""

from __future__ import annotations

from pathlib import Path

from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.emit.matrix import emit_matrix_descriptor
from ptx_frontend.code_gen.emit.availability import emit_availability
from ptx_frontend.code_gen.resolved_layout import operand_slots
from ptx_frontend.code_gen.resolved_field_names import (
    condition_code_cpp_value,
    field_cpp_constant_expr,
    field_cpp_type,
)
from ptx_frontend.ir.resolved_ir import ResolvedField, ResolvedFieldStorage
from ptx_frontend.ir.tensor_reduction import (
    TENSOR_REDUCTION_ELEMENT_TYPES, TensorReductionOp,
)
from ptx_frontend.spec.model import (
    OpaqueResourceKind,
    SemanticRule,
    TextureComponent,
    TextureMipmapMode,
    TextureQuery,
)


INCLUDE_ROOT = "ptx_frontend/resolved_ir"
FORM_SHARD_SIZE = 64


def _fabric_descriptor(variant) -> str:
    """Emit typed, immutable CFT obligations from the normalized source row."""

    contract = variant.fabric
    if contract is None:
        return ""
    operation = file_stem_to_pascal_case(contract.operation.value)
    endpoint = file_stem_to_pascal_case(contract.endpoint.value)
    shared_access = file_stem_to_pascal_case(contract.shared_access.value)
    completion = ''.join(part.title() for part in variant.completion_kind.value.split('_'))
    counted = str(contract.counted).lower()
    reports = str(contract.reports_fabric).lower()
    layout = ("base::MbarrierLayout::V1" if contract.requires_mbarrier_layout_v1
              else "std::nullopt")
    return f'''  /** Static CFT endpoint, completion, reporting and shared-access contract. */
  inline static constexpr FabricInstructionDescriptor fabric_contract{{
      .operation = FabricOperation::{operation},
      .endpoint = FabricEndpointKind::{endpoint},
      .shared_access = FabricSharedAccess::{shared_access},
      .completion = base::AsyncCompletionKind::{completion},
      .counted = {counted},
      .reports_fabric = {reports},
      .required_mbarrier_layout = {layout},
  }};
'''


_TEXTURE_GEOMETRY_CPP = {
    "1d": "OneD", "2d": "TwoD", "3d": "ThreeD",
    "a1d": "ArrayOneD", "a2d": "ArrayTwoD", "cube": "Cube",
    "acube": "ArrayCube", "2dms": "TwoDMultisample",
    "a2dms": "ArrayTwoDMultisample",
}


def _surface_descriptor(variant) -> str:
    """Emit immutable surface topology plus its mutable selected data type."""
    contract = variant.surface
    if contract is None:
        return ""
    def selected(domain: str, value) -> str:
        """Spell a closed enum value or an absent form fact."""
        if value is None:
            return "std::nullopt"
        name = ({"b": "Byte", "p": "Sample"}.get(value.value)
                if domain == "SurfaceAddressingMode" else None)
        if domain == "SurfaceGeometry":
            name = _TEXTURE_GEOMETRY_CPP[value.value]
        return domain + "::" + (name or file_stem_to_pascal_case(value.value))
    gate = emit_availability(contract.indirect_availability)
    return f'''  /** Canonical gate for a scalar register-carried surface resource. */
  inline static constexpr checker::AvailabilityDescriptor indirect_gate{gate};
  /** Immutable exact-form surface semantics, independent of texture mode. */
  inline static constexpr SurfaceInstructionDescriptor surface_contract{{
      .geometry = {selected("SurfaceGeometry", contract.geometry)},
      .addressing = {selected("SurfaceAddressingMode", contract.addressing)},
      .boundary = {selected("SurfaceBoundaryMode", contract.boundary)},
      .operation = {selected("SurfaceReductionOperation", contract.operation)},
      .query = {selected("SurfaceQuery", contract.query)},
      .vector_arity = {contract.vector_arity},
      .indirect_availability = &indirect_gate,
  }};
  /** Borrow immutable exact-form surface semantics. */
  const SurfaceInstructionDescriptor* surface_descriptor() const noexcept override {{
    return &surface_contract;
  }}
  /** Read the current mutable data type without consulting source syntax. */
  SurfaceSelectedTypes surface_selected_types() const noexcept override {{
    return {{.data_type = dtype.value}};
  }}
'''


def _texture_descriptor(variant) -> str:
    """Emit the closed exact-form texture semantics exposed to consumers."""

    contract = variant.texture
    if contract is None:
        return ""
    geometry = (
        "TextureGeometry::" + _TEXTURE_GEOMETRY_CPP[contract.geometry.value]
        if contract.geometry is not None else "std::nullopt"
    )
    mipmap = {
        TextureMipmapMode.OMITTED: "Omitted",
        TextureMipmapMode.BASE: "Base",
        TextureMipmapMode.LEVEL: "Level",
        TextureMipmapMode.GRADIENT: "Gradient",
    }[contract.mipmap]
    component = (
        "TextureComponent::" + {
            TextureComponent.RED: "Red",
            TextureComponent.GREEN: "Green",
            TextureComponent.BLUE: "Blue",
            TextureComponent.ALPHA: "Alpha",
        }[contract.component]
        if contract.component is not None else "std::nullopt"
    )
    query = (
        "TextureQuery::" + file_stem_to_pascal_case(contract.query.value).replace("AddrMode", "AddressMode")
        if contract.query is not None else "std::nullopt"
    )
    tested = (
        "base::OpaqueResourceKind::" + {
            OpaqueResourceKind.TEXTURE: "Texture",
            OpaqueResourceKind.SAMPLER: "Sampler",
            OpaqueResourceKind.SURFACE: "Surface",
        }[contract.tested_kind]
        if contract.tested_kind is not None else "std::nullopt"
    )
    selected_types = (
        """  /** Return current source-selected type modifiers for AST-free checks. */
  TextureSelectedTypes texture_selected_types() const noexcept override {
    return {.result_type = dtype.value, .coordinate_type = ctype.value};
  }
"""
        if {field.source_name for field in variant.modifier_fields} >= {"dtype", "ctype"}
        else ""
    )
    indirect_gate = (
        "  /** Minimum source and target for a register-carried opaque resource. */\n"
        "  inline static constexpr checker::AvailabilityDescriptor indirect_gate"
        + emit_availability(contract.indirect_availability) + ";\n"
        if contract.indirect_availability is not None else ""
    )
    residency_layout = ""
    if contract.geometry is not None:
        cases = "\n".join(
            f"      case {index}: return {str(any(binding.texture_residency_required for binding in layout.bindings)).lower()};"
            for index, layout in enumerate(variant.operand_layouts)
        )
        residency_layout = f"""  /** Return predicate presence required by the selected source layout. */
  bool texture_layout_requires_residency() const noexcept override {{
    switch (operand_layout.value) {{
{cases}
      default: return false;
    }}
  }}
"""
    return f'''  /** Immutable PTX texture-family semantics of this exact form. */
{indirect_gate}
  inline static constexpr TextureInstructionDescriptor texture_contract{{
      .geometry = {geometry},
      .mipmap = TextureMipmapMode::{mipmap},
      .component = {component},
      .query = {query},
      .tested_kind = {tested},
      .result_arity = {contract.result_arity},
      .allows_residency = {str(contract.allows_residency).lower()},
      .allows_offset = {str(contract.allows_offset).lower()},
      .allows_compare = {str(contract.allows_compare).lower()},
      .query_level = {str(contract.query_level).lower()},
      .indirect_availability = {"&indirect_gate" if indirect_gate else "nullptr"},
  }};
  /** Borrow this form's static topology for read-only consumers. */
  const TextureInstructionDescriptor* texture_descriptor() const noexcept override {{
    return &texture_contract;
  }}
{selected_types}
{residency_layout}
'''


def form_shards(entry: GenerationInstruction) -> tuple[tuple[int, ...], ...]:
    """Partition large opcode forms in canonical order for bounded generation."""

    count = len(entry.resolved.variants)
    if count <= FORM_SHARD_SIZE:
        return ()
    return tuple(
        tuple(range(start, min(start + FORM_SHARD_SIZE, count)))
        for start in range(0, count, FORM_SHARD_SIZE)
    )


def form_name(entry: GenerationInstruction, variant) -> str:
    """Return the unique final C++ identity of one semantic form."""

    return entry.cpp_name + variant.cpp_name


def method_name(cpp_type: str) -> str:
    """Return the stable observer callback for one foundation reference type."""

    if cpp_type not in REFERENCE_TYPES:
        raise ValueError(f"reference payload lacks typed observer callback: {cpp_type}")
    aliases = {
        "ResolvedPredicate": "predicate",
        "ResolvedRegisterRef": "reg",
        "RegOrImm": "reg_or_imm",
    }
    if cpp_type in aliases:
        return aliases[cpp_type]
    text = cpp_type.removeprefix("Resolved")
    result = ""
    for index, char in enumerate(text):
        if char.isupper() and index and (
            text[index - 1].islower()
            or (index + 1 < len(text) and text[index + 1].islower())
        ):
            result += "_"
        result += char.lower()
    return result


REFERENCE_TYPES = (
    "RegOrImm", "ResolvedAddress", "ResolvedBranchTarget",
    "ResolvedBranchTargetSet", "ResolvedCallArguments", "ResolvedCallParameterRef",
    "ResolvedCpAsyncSourceControl", "ResolvedFunctionRef", "ResolvedIndirectCallee",
    "ResolvedMbarrierStateToken", "ResolvedMovSource", "ResolvedPredicate",
    "ResolvedPredicateOrSink", "ResolvedPredicatePair", "ResolvedPredicatePairOrSink",
    "ResolvedPredicateSource", "ResolvedRegisterOrSink", "ResolvedRegisterRef",
    "ResolvedRegisterVector", "ResolvedShflSyncDestination", "ResolvedSymbolRef",
    "ResolvedValueVector",
    "ResolvedTensorCoordinate", "ResolvedTensorIm2colInfo", "ResolvedTensorOperand",
    "ResolvedSurfaceAccess", "ResolvedSurfaceQueryResource",
    "ResolvedFabricHandle", "ResolvedTextureAccess", "ResolvedTextureQueryResource",
    "ResolvedTextureResult",
    "TensorMemoryAddress", "ResolvedMatrixScaleSelector",
    "ResolvedSharedMatrixDescriptor", "ResolvedVectorRegisterRef",
)


def generate_resolved_base_header(
    context: GenerationContext, *, output_path: Path
) -> None:
    """Emit common polymorphic identity and typed borrowed-reference contracts."""

    from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
    from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type

    for entry in context.entries:
        for variant in entry.resolved.variants:
            for layout in variant.operand_layouts:
                for field in layout.fields:
                    if field.value_kind in REFERENCE_VALUE_KINDS:
                        method_name(field_value_cpp_type(field, backend=context.backend))
    callbacks = "\n".join(
        f"  /** Observe a borrowed {cpp_type} and its source locations synchronously. */\n"
        f"  virtual void {method_name(cpp_type)}(const {cpp_type}&, "
        "std::span<const SourceRange>,\n"
        "      checker::AddressSymbolResolutionPolicy) {}"
        for cpp_type in REFERENCE_TYPES
    )
    reduction_domain = (
        _emit_tensor_reduction_domain()
        if any(variant.tensor_reduction_op is not None
               for entry in context.entries for variant in entry.resolved.variants)
        else ""
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <{INCLUDE_ROOT}/ptx_resolved_ir_checker_support.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_diagnostics.hpp>

namespace ptx_frontend::syntax_ast {{
struct AstInstruction;
}}  // namespace ptx_frontend::syntax_ast

namespace ptx_frontend::resolved_ir {{

struct ResolveContext;

/** Opcode-family identity; enumerators live in the optional broad catalogue. */
enum class Opcode : std::uint32_t;

/** Exact semantic form identity; enumerators live in the broad catalogue. */
enum class InstructionKind : std::uint32_t;

{reduction_domain}

namespace detail {{
/** Internal synchronous observer of borrowed typed reference payloads.
 * A callback may not retain its references or spans or reenter payload mutation.
 */
struct IReferenceObserver {{
  /** Destroy a caller-owned concrete observer through this interface. */
  virtual ~IReferenceObserver() = default;
{callbacks}
}};
}}  // namespace detail

/** Owned common instruction state with exact polymorphic semantic identity. */
class Instruction {{
 public:
  /** Mark an exact final instruction type for constrained queries. */
  static constexpr bool is_instruction = true;
  /** Owned execution guard and source provenance, visited before operands. */
  std::optional<WithLocs<ResolvedPredicate>> execution_predicate;
  /** Destroy the exact derived payload. */
  virtual ~Instruction();
  /** Allocate an independent deep copy of all owned state. */
  virtual std::unique_ptr<Instruction> clone() const = 0;
  /** Return the exact immutable semantic identity of this object. */
  virtual InstructionKind instruction_kind() const noexcept = 0;
  /** Return its opcode-family identity. */
  Opcode opcode_kind() const noexcept;
  /** Return its canonical opcode mnemonic. */
  std::string_view opcode_name() const noexcept;
  /** Return immutable surface-family facts, or null for other instructions. */
  virtual const SurfaceInstructionDescriptor* surface_descriptor() const noexcept {{
    return nullptr;
  }}
  /** Return current type modifiers for surface forms. */
  virtual SurfaceSelectedTypes surface_selected_types() const noexcept {{
    return {{}};
  }}
  /** Return immutable texture-family facts, or null for other instructions. */
  virtual const TextureInstructionDescriptor* texture_descriptor() const noexcept {{
    return nullptr;
  }}
  /** Return current type modifiers for texture access forms. */
  virtual TextureSelectedTypes texture_selected_types() const noexcept {{
    return {{}};
  }}
  /** Return predicate presence required by this mutable operand layout. */
  virtual bool texture_layout_requires_residency() const noexcept {{
    return false;
  }}
  /** Check mutable fields without module-level context. */
  virtual checker::CheckResult check(const checker::Context&) const = 0;
  /** Borrow references in predicate-first descriptor operand order. */
  virtual void visit_references(detail::IReferenceObserver&) const = 0;

 protected:
  /** Construct an instruction with no execution guard. */
  Instruction() = default;
  /** Copy common owned state for a concrete clone. */
  Instruction(const Instruction&) = default;
  /** Move common owned state. */
  Instruction(Instruction&&) = default;
  /** Copy common owned state. */
  Instruction& operator=(const Instruction&) = default;
  /** Move common owned state. */
  Instruction& operator=(Instruction&&) = default;
}};

/** Resolve syntax to one owned exact final instruction. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveInstruction(
    const syntax_ast::AstInstruction&);
/** Resolve syntax using a bound declaration context. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveInstruction(
    const syntax_ast::AstInstruction&, const ResolveContext&);

}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")


def _identity_namespace(entry: GenerationInstruction) -> str:
    """Return the opcode-local C++ namespace shared by identity consumers."""

    category = file_stem_to_pascal_case(entry.specification.codegen_category)
    return f"identity::{category}::{entry.cpp_name}"


def generate_resolved_opcode_identity_header(
    context: GenerationContext, *, category: str, opcode: str, output_path: Path
) -> None:
    """Emit named typed identities for one complete opcode without its classes."""

    entry = next(
        item for item in context.entries
        if item.specification.codegen_category == category
        and item.specification.opcode == opcode
    )
    forms = "\n".join(
        f"/** Exact identity of the {form_name(entry, variant)} form. */\n"
        f"inline constexpr InstructionKind {form_name(entry, variant)} =\n"
        f"    static_cast<InstructionKind>("
        f"0x{context.identities.form_value(entry, variant):08x}u);"
        for variant in entry.resolved.variants
    )
    namespace = _identity_namespace(entry)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#pragma once

#include <{INCLUDE_ROOT}/ptx_instruction_base.hpp>

namespace ptx_frontend::resolved_ir::{namespace} {{

/** Opcode-family identity shared by all forms in this header. */
inline constexpr Opcode opcode =
    static_cast<Opcode>(0x{context.identities.opcode_value(entry):08x}u);

{forms}

}}  // namespace ptx_frontend::resolved_ir::{namespace}
""", encoding="utf-8")


def generate_resolved_identity_catalogue_header(
    context: GenerationContext, *, output_path: Path
) -> None:
    """Emit explicit complete enums without forcing them into narrow headers."""

    includes = "\n".join(
        f"#include <{INCLUDE_ROOT}/identity/"
        f"{entry.specification.codegen_category}/{entry.specification.opcode}.gen.hpp>"
        for entry in context.entries
    )
    opcodes = "\n".join(
        f"  {entry.cpp_name} = static_cast<std::uint32_t>("
        f"{_identity_namespace(entry)}::opcode),"
        for entry in context.entries
    )
    forms = "\n".join(
        f"  {form_name(entry, variant)} = "
        f"static_cast<std::uint32_t>("
        f"{_identity_namespace(entry)}::{form_name(entry, variant)}),"
        for entry in context.entries for variant in entry.resolved.variants
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#pragma once

#include <{INCLUDE_ROOT}/ptx_instruction_base.hpp>
{includes}

namespace ptx_frontend::resolved_ir {{

/** Complete opcode names; category IDs are stable, opcode ordinals are local. */
enum class Opcode : std::uint32_t {{
{opcodes}
}};

/** Complete form names; form ordinals follow each opcode's canonical order. */
enum class InstructionKind : std::uint32_t {{
{forms}
}};

}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")


def _field_declaration(field: ResolvedField, backend, name: str) -> str:
    """Emit one direct modifier or operand member with its ownership contract."""

    cpp_type = field_cpp_type(field, backend=backend)
    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        return (
            f"  /** Semantic constant implied by this form. */\n"
            f"  inline static constexpr {cpp_type} {name} = "
            f"{field_cpp_constant_expr(field, backend=backend)};"
        )
    return f"  /** Owned resolved {field.name} value and source locations. */\n  {cpp_type} {name};"


def _tensor_cta_group_contract(variant) -> str:
    """Emit a copied written/effective CTA-group role for exact tensor forms."""

    if variant.tensor_access_mode is None:
        return ""
    if not variant.tensor_cta_group_applicable:
        return """  /** This tensor form has no CTA-group routing role. */
  std::optional<TensorCtaGroupRole> tensor_cta_group_role() const noexcept {
    return std::nullopt;
  }"""
    one = "MulticastDestinations" if variant.tensor_multicast else "Destination"
    two = "MulticastParityPeers" if variant.tensor_multicast else "DestinationOrPeer"
    if not any(field.source_name == "cta_group" for field in variant.modifier_fields):
        return f"""  /** Omitted spelling defaults to group one on this tensor load. */
  std::optional<TensorCtaGroupRole> tensor_cta_group_role() const {{
    return TensorCtaGroupRole{{.spelled = std::nullopt,
                               .effective = TensorCtaGroup::One,
                               .routing = TensorCtaSignalRouting::{one}}};
  }}"""
    return f"""  /** Copy written group and mbarrier-routing role after source checks. */
  std::optional<TensorCtaGroupRole> tensor_cta_group_role() const {{
    if (cta_group.locs.empty() ||
        (cta_group.value != TensorCtaGroup::One &&
         cta_group.value != TensorCtaGroup::Two)) return std::nullopt;
    return TensorCtaGroupRole{{.spelled = cta_group,
                               .effective = cta_group.value,
                               .routing = cta_group.value == TensorCtaGroup::One
                                   ? TensorCtaSignalRouting::{one}
                                   : TensorCtaSignalRouting::{two}}};
  }}"""


def _tensor_map_replace_contract(variant) -> str:
    """Project canonical fixed tensor-map replacement field and encoded values."""

    fields = [field.source_name.removeprefix("field_")
              for field in variant.modifier_fields
              if field.source_name.startswith("field_")]
    if not fields:
        return ""
    if len(fields) != 1:
        raise ValueError("tensor-map replacement must have one fixed field")
    field = fields[0]
    value_type = {
        "elemtype": "TensorMapElementType",
        "interleave_layout": "TensorMapInterleaveLayout",
        "swizzle_mode": "TensorMapSwizzleMode",
        "swizzle_atomicity": "TensorMapSwizzleAtomicity",
        "fill_mode": "TensorMapFillMode",
    }.get(field)
    encoded = (
        f"""  /** Decode the original code after owned-value consistency checks. */
  std::optional<{value_type}> encoded_value() const noexcept {{
    return project_tensor_map_encoded_value<{value_type}>(
        replacement_field, new_val.value);
  }}"""
        if value_type else ""
    )
    return f"""  /** Closed identity of this encoded tensor-map field. */
  inline static constexpr TensorMapReplaceField replacement_field =
      TensorMapReplaceField::{file_stem_to_pascal_case(field)};
  /** Copy an opaque descriptor reference when source locations are present. */
  std::optional<ResolvedTensorMapRef> tensor_map_ref() const {{
    if (tensor_map.locs.empty()) return std::nullopt;
    return ResolvedTensorMapRef{{tensor_map.value, tensor_map.locs.front()}};
  }}
{encoded}"""


def _form_contract(variant, backend) -> str:
    """Emit immutable exact-form semantic facts without opcode wrappers."""

    parts = []
    if variant.tensor_reduction_op is not None:
        parts.append(
            "  /** Closed tiled tensor-reduction operation. */\n"
            "  inline static constexpr TensorReductionOp tensor_reduction_op = "
            f"TensorReductionOp::{file_stem_to_pascal_case(variant.tensor_reduction_op.value)};"
        )
    group = _tensor_cta_group_contract(variant)
    if group:
        parts.append(group)
    replacement = _tensor_map_replace_contract(variant)
    if replacement:
        parts.append(replacement)
    if variant.matrix is not None:
        parts.append(
            "  /** Canonical topology independent of mutable operand state. */\n"
            "  inline static constexpr MatrixInstructionDescriptor matrix_topology =\n      "
            + emit_matrix_descriptor(variant.matrix, backend) + ";\n"
            "  /** Borrow the immutable topology of this exact final form. */\n"
            "  const MatrixInstructionDescriptor* matrix_descriptor() const noexcept {\n"
            "    return &matrix_topology;\n  }"
        )
    if variant.surface is not None:
        parts.append(_surface_descriptor(variant))
    if variant.texture is not None:
        parts.append(_texture_descriptor(variant))
    parts.extend(_tcgen_form_contract(variant))
    return "\n".join(parts)


def _tcgen_form_contract(variant) -> list[str]:
    """Expose closed Tensor Memory form facts without a mutable opcode wrapper."""

    result: list[str] = []
    allocation = {
        SemanticRule.TENSOR_MEMORY_ALLOC: ("Alloc", "RequiresPermit"),
        SemanticRule.TENSOR_MEMORY_DEALLOC: ("Dealloc", "ReleasesAllocation"),
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT:
            ("RelinquishAllocPermit", "RelinquishesPermit"),
    }
    if variant.rule in allocation:
        action, effect = allocation[variant.rule]
        result.append(
            "  /** Canonical allocation-management action. */\n"
            f"  inline static constexpr TcgenAllocationAction allocation_action = TcgenAllocationAction::{action};\n"
            "  /** Local permit obligation/effect; no CFG state is inferred. */\n"
            f"  inline static constexpr TcgenAllocationPermitEffect permit_effect = TcgenAllocationPermitEffect::{effect};"
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_COMMIT:
        spelling = variant.tcgen_commit_address_spelling
        if spelling is None:
            raise ValueError("TCGEN commit lacks address spelling")
        result.append(
            "  /** Written qualifier; barrier access uses generic proxy. */\n"
            f"  inline static constexpr TcgenCommitAddressSpelling address_spelling = TcgenCommitAddressSpelling::{file_stem_to_pascal_case(spelling.value)};\n"
            "  /** Whether a mask selects peer-CTA barriers. */\n"
            f"  inline static constexpr bool multicast = {'true' if variant.tcgen_commit_multicast else 'false'};\n"
            "  /** Cluster-scoped arrive-on count. */\n"
            "  inline static constexpr uint8_t arrive_count = 1;\n"
            "  /** Mbarrier signal scope. */\n"
            "  inline static constexpr base::MemoryScope signal_scope = base::MemoryScope::Cluster;\n"
            "  /** Barrier access uses the generic proxy. */\n"
            "  inline static constexpr bool generic_proxy_access = true;"
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_FENCE:
        direction = variant.tcgen_fence_direction
        if direction is None:
            raise ValueError("TCGEN fence lacks direction")
        result.append(
            "  /** Specialized ordering direction; not a completion promise. */\n"
            f"  inline static constexpr TcgenFenceDirection direction = TcgenFenceDirection::{file_stem_to_pascal_case(direction.value)};"
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_COPY:
        shape_names = {
            "32x32b": "S32x32b", "16x64b": "S16x64b", "16x128b": "S16x128b",
            "16x256b": "S16x256b", "16x32bx2": "S16x32bx2",
            "s128x256b": "S128x256b", "s4x256b": "S4x256b",
            "s128x128b": "S128x128b", "s64x128b": "S64x128b", "s32x128b": "S32x128b",
        }
        multicast_names = {
            "none": "None", "warpx2_02_13": "WarpX2_02_13",
            "warpx2_01_23": "WarpX2_01_23", "warpx4": "WarpX4",
        }
        pairs = ",\n".join(
            "      {TcgenDataMovementShape::%s, TcgenCopyMulticast::%s}" %
            (shape_names[shape], multicast_names[multicast])
            for shape, multicast in variant.tcgen_copy_pairs
        )
        masks = ", ".join(
            str(sum(1 << index for index, present in enumerate(row) if present))
            for row in variant.tcgen_copy_formats
        )
        result.append(
            "  /** Canonical shape/multicast rows in static storage. */\n"
            f"  inline static constexpr TcgenCopyShapePair copy_pairs[] = {{\n{pairs}\n  }};\n"
            "  /** Destination/source format masks in field order. */\n"
            f"  inline static constexpr uint8_t copy_format_masks[] = {{{masks}}};\n"
            "  /** Opaque Table 43 source role, without bit decoding. */\n"
            "  inline static constexpr bool has_tcgen_copy_descriptor = true;"
        )
        result.append(
            "  /** Borrow a validated opaque descriptor role from the owned copy. */\n"
            "  std::optional<TcgenCopyDescriptorView> descriptor_view() const noexcept {\n"
            "    return tcgen_copy_descriptor_view(s_desc.value);\n  }"
        )
    return result


def _opcode_entrypoints(entry: GenerationInstruction) -> str:
    """Declare an opcode's metadata and resolver once in its public aggregate."""

    opcode = entry.specification.opcode
    prefix = opcode.replace(".", "_").replace("-", "_")
    return f"""/** Return static-lifetime syntax selection metadata for {opcode}. */
const check_end::SyntaxInstructionDescriptor& {prefix}_syntax_descriptor() noexcept;
/** Return static-lifetime resolved field metadata for {opcode}. */
const check_end::ResolvedInstructionDescriptor& {prefix}_resolved_descriptor() noexcept;
/** Return static-lifetime legality metadata for {opcode}. */
const checker::InstructionDescriptor& {prefix}_checker_descriptor() noexcept;
/** Resolve one {opcode} syntax instruction to its exact final form. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolve{entry.cpp_name}(
    const syntax_ast::AstInstruction&, const ResolveContext* = nullptr);"""


def generate_resolved_opcode_header(
    context: GenerationContext, *, category: str, opcode: str, output_path: Path,
    include_entrypoints: bool = True,
    form_indices: tuple[int, ...] | None = None,
) -> None:
    """Emit selected full-context forms, with declarations in the aggregate."""

    entries = tuple(
        entry for entry in context.entries
        if entry.specification.codegen_category == category
        and entry.specification.opcode == opcode
    )
    if len(entries) != 1:
        raise ValueError(f"expected one {category}/{opcode} entry")
    entry = entries[0]
    shards = form_shards(entry)
    if shards and form_indices is None:
        includes = "\n".join(
            f"#include <{INCLUDE_ROOT}/model/{category}/"
            f"{opcode}_forms_{index:03d}.gen.hpp>"
            for index in range(len(shards))
        )
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            "// Generated by ptx_frontend resolved IR code generation. Do not edit.\n"
            "#pragma once\n\n" + includes + "\n\n"
            "namespace ptx_frontend::resolved_ir {\n\n"
            + _opcode_entrypoints(entry)
            + "\n\n}  // namespace ptx_frontend::resolved_ir\n",
            encoding="utf-8",
        )
        return
    definitions: list[str] = []
    indices = (range(len(entry.resolved.variants))
               if form_indices is None else form_indices)
    for index in indices:
        variant = entry.resolved.variants[index]
        name = form_name(entry, variant)
        contract = _form_contract(variant, context.backend)
        fabric_descriptor = _fabric_descriptor(variant)
        slots = operand_slots(variant, context.backend)
        modifiers = "\n".join(
            _field_declaration(field, context.backend, field.name)
            for field in variant.modifier_fields
        )
        operands = "\n".join(
            (
                f"  /** Layout-specific owned {slot.field.name}; engaged exactly "
                "for layouts that bind it. */\n"
                f"  std::optional<{field_cpp_type(slot.field, backend=context.backend)}> "
                f"{slot.member_name};"
            ) if slot.optional else _field_declaration(
                slot.field, context.backend, slot.member_name
            )
            for slot in slots
        )
        atomic = (
            "  /** Written atomic address suffix and source provenance. */\n"
            "  WithLocs<AtomicAddressQualifier> address_qualifier;\n"
            if entry.resolved.atomic_address_qualifier is not None else ""
        )
        definitions.append(f"""/** Exact {opcode} {variant.cpp_name} form; operands are direct typed members. */
class {name} final : public Instruction {{
 public:
  /** Fixed opcode family without per-instance storage. */
  static constexpr Opcode opcode = {_identity_namespace(entry)}::opcode;
  /** Fixed semantic identity without per-instance storage. */
  static constexpr InstructionKind kind =
      {_identity_namespace(entry)}::{name};
  /** Implicit CC.CF effect under the execution predicate. */
  inline static constexpr ConditionCodeEffect condition_code_effect =
      {condition_code_cpp_value(variant.condition_code_effect)};
  /** Instruction-local asynchronous completion identity. */
  inline static constexpr base::AsyncCompletionKind completion_kind =
      base::AsyncCompletionKind::{''.join(part.title() for part in variant.completion_kind.value.split('_'))};
  /** Programmer-expressed WGMMA action; sequence obligations remain external. */
  inline static constexpr base::WgmmaProtocolAction wgmma_protocol_action =
      base::WgmmaProtocolAction::{''.join(part.title() for part in variant.wgmma_protocol_action.value.split('_'))};
{contract}
{fabric_descriptor}
  /** Selected layout identity and resolution provenance. */
  ResolvedOperandLayoutTag operand_layout;
{atomic}{modifiers}
{operands}
  /** Construct a mutable resolved form before fields are populated. */
  {name}() = default;
  /** Return the exact semantic identity. */
  InstructionKind instruction_kind() const noexcept override;
  /** Deep-copy all common and concrete owned state. */
  std::unique_ptr<Instruction> clone() const override;
  /** Check this form and its selected operand layout. */
  checker::CheckResult check(const checker::Context&) const override;
  /** Borrow references synchronously in predicate-first order. */
  void visit_references(detail::IReferenceObserver&) const override;
}};""")
    definition = "\n\n".join(definitions)
    entrypoints = _opcode_entrypoints(entry) if include_entrypoints else ""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <{INCLUDE_ROOT}/identity/{category}/{opcode}.gen.hpp>
#include <{INCLUDE_ROOT}/ptx_instruction_base.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_selection.hpp>

namespace ptx_frontend::resolved_ir {{

{definition}

{entrypoints}

}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")


def _emit_tensor_reduction_domain() -> str:
    """Emit the closed conditional operation/type query, not descriptor decoding."""

    enum_members = "\n".join(
        f"  {file_stem_to_pascal_case(op.value)}," for op in TensorReductionOp
    )
    cases = "\n".join(
        "    case TensorReductionOp::"
        f"{file_stem_to_pascal_case(op.value)}:\n"
        "      switch (element_type) {\n"
        + "\n".join(
            f"        case base::ScalarType::{scalar.upper()}:"
            for scalar in TENSOR_REDUCTION_ELEMENT_TYPES[op]
        )
        + "\n          return true;\n"
        "        default:\n          return false;\n      }"
        for op in TensorReductionOp
    )
    return f"""/** Encoded tiled tensor-reduction operation; descriptor contents remain opaque. */
enum class TensorReductionOp : uint8_t {{
{enum_members}
}};

/** Whether an operation permits a descriptor element type if that type is known.
 * This does not inspect or validate a tensor-map descriptor instance.
 */
constexpr bool tensor_reduction_accepts_element_type(
    TensorReductionOp operation, base::ScalarType element_type) noexcept {{
  switch (operation) {{
{cases}
    default:
      return false;
  }}
}}
"""


def generate_resolved_form_shard_header(
    context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path,
) -> None:
    """Emit one canonical slice of exact public classes from the same model."""

    entry = next(
        item for item in context.entries
        if item.specification.codegen_category == category
        and item.specification.opcode == opcode
    )
    indices = form_shards(entry)[shard_index]
    generate_resolved_opcode_header(
        context, category=category, opcode=opcode, output_path=output_path,
        include_entrypoints=False, form_indices=indices,
    )


def generate_resolved_umbrella_header(
    context: GenerationContext, *, output_path: Path
) -> None:
    """Expose all generated final forms through one installed aggregate header."""

    includes = "\n".join(
        f"#include <{INCLUDE_ROOT}/model/"
        f"{entry.specification.codegen_category}/{entry.specification.opcode}.gen.hpp>"
        for entry in context.entries
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#pragma once

#include <{INCLUDE_ROOT}/ptx_instruction_base.hpp>
#include <{INCLUDE_ROOT}/ptx_instruction_catalogue.gen.hpp>
{includes}
""", encoding="utf-8")
