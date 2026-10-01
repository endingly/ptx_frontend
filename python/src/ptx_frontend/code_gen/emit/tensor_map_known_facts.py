"""Render the C++ known-fact query from its sole rule catalog.

The standard generation plan owns both public metadata and the private query.
Selected-form projection never makes caller claims into descriptor contents.
"""

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext

from ptx_frontend.spec.tensor_map_known_facts import RULE_CATALOG, TensorFactRule
from ptx_frontend.ir.resolved_ir import (
    ResolvedOperandAccess,
    ResolvedValueKind,
    TensorAccessMode,
    tensor_im2col_info_contract,
)


_CPP_TENSOR_MODE = {
    TensorAccessMode.TILED: "Tiled",
    TensorAccessMode.IM2COL_NO_OFFS: "Im2colNoOffs",
    TensorAccessMode.IM2COL: "Im2col",
    TensorAccessMode.IM2COL_W: "Im2colW",
    TensorAccessMode.IM2COL_W128: "Im2colW128",
    TensorAccessMode.TILE_GATHER4: "TileGather4",
    TensorAccessMode.TILE_SCATTER4: "TileScatter4",
}


def _cpp_string(value: str) -> str:
    """Escape catalog prose for one deterministic C++ string literal."""

    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_tensor_map_known_fact_rules() -> str:
    """Produce closed public C++ IDs and metadata from the rule catalog.

    The installed header has a companion private query source and no second
    Table 33 table.
    """

    inputs = sorted({name for entry in RULE_CATALOG.values()
                     for name in entry.inputs})
    if len(inputs) > 64:
        raise ValueError("known-fact dependency metadata exceeds uint64_t")
    input_bits = {name: 1 << index for index, name in enumerate(inputs)}
    rows = []
    for rule in TensorFactRule:
        entry = RULE_CATALOG[rule]
        mask = 0
        for name in entry.inputs:
            mask |= input_bits[name]
        rows.append(
            "    {TensorMapFactRule::" + rule.name + ", " +
            _cpp_string(entry.title) + ", " +
            _cpp_string(entry.source_section) + ", UINT64_C(" +
            str(mask) + ")},"
        )
    field_rows = [
        "    {" + _cpp_string(name) + ", UINT64_C(" +
        str(input_bits[name]) + ")},"
        for name in inputs
    ]
    enum_rows = ["  " + rule.name + " = " + str(rule.value) + ","
                 for rule in TensorFactRule]
    return "\n".join([
        "// Generated from the focused tensor-map known-facts catalog.",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>",
        "namespace ptx_frontend::resolved_ir {",
        "/** One finite caller-fact relation; not a whole-descriptor verdict. */",
        "enum class TensorMapFactRule : uint8_t {",
        *enum_rows,
        "};",
        "/** Catalog row with exact source and required fact-name bitset. */",
        "struct TensorMapFactRuleDescriptor {",
        "  TensorMapFactRule rule;",
        "  std::string_view title;",
        "  std::string_view source_section;",
        "  uint64_t input_mask;",
        "};",
        "/** One dependency name and its stable bit in this generated table. */",
        "struct TensorMapFactInputDescriptor {",
        "  std::string_view name;",
        "  uint64_t bit;",
        "};",
        "inline constexpr std::array<TensorMapFactInputDescriptor, " +
        str(len(inputs)) + "> tensor_map_fact_inputs{{",
        *field_rows,
        "}};",
        "inline constexpr std::array<TensorMapFactRuleDescriptor, " +
        str(len(TensorFactRule)) + "> tensor_map_fact_rules{{",
        *rows,
        "}};",
        _render_info_bounds(),
        "}  // namespace ptx_frontend::resolved_ir",
        "",
    ])


def render_tensor_map_known_fact_query() -> str:
    """Prepare one out-of-line query source for the standard global plan."""

    return "\n".join([
        "// Generated from the focused tensor-map known-facts catalog.",
        "#include <algorithm>",
        "#include <array>",
        "#include <cstdint>",
        "#include <string>",
        "#include <string_view>",
        "#include <type_traits>",
        "#include <utility>",
        "#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>",
        "#include <ptx_frontend/resolved_ir/ptx_tensor_map_known_facts.hpp>",
        "#include <ptx_frontend/resolved_ir/tensor_map_known_facts.gen.hpp>",
        "namespace ptx_frontend::resolved_ir {",
        _CPP_QUERY,
        "}  // namespace ptx_frontend::resolved_ir",
        "",
    ])


def generate_tensor_map_known_fact_rules(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the public deterministic catalog artifact in the standard plan."""

    del context
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tensor_map_known_fact_rules(), encoding="utf-8")


def generate_tensor_map_known_fact_query(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the private query and owned selected-form adapter together."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        render_tensor_map_known_fact_query() + _render_selected_adapter(context),
        encoding="utf-8",
    )


def _render_selected_adapter(context: GenerationContext) -> str:
    """Emit selected Cp metadata from normalized typed variants only."""

    copies = [entry.resolved for entry in context.entries
              if entry.specification.opcode == "cp"]
    if len(copies) != 1:
        raise ValueError("known-fact adapter requires one normalized Cp model")
    rows = []
    for variant in copies[0].variants:
        if variant.tensor_access_mode is None:
            rows.append("    {false, TensorKnownFactDirection::Load, "
                        "TensorAccessMode::Tiled, TensorRank::One, "
                        "std::nullopt, false},")
            continue
        ranks = {
            binding.expected_tensor_rank
            for layout in variant.operand_layouts for binding in layout.bindings
            if binding.tensor_access_mode is not None
        }
        if len(ranks) != 1 or None in ranks:
            raise ValueError("tensor variant lacks one normalized rank")
        rank = next(iter(ranks))
        if variant.tensor_reduction_op is not None:
            direction = "Reduce"
        elif any(field.value_kind is ResolvedValueKind.ADDRESS and
                 field.operand_access is ResolvedOperandAccess.WRITE
                 for layout in variant.operand_layouts for field in layout.fields):
            direction = "Load"
        elif any(field.value_kind is ResolvedValueKind.ADDRESS and
                 field.operand_access is ResolvedOperandAccess.READ
                 for layout in variant.operand_layouts for field in layout.fields):
            direction = "Store"
        else:
            direction = "Prefetch"
        destination = variant.tensor_destination
        if (direction == "Load") != (destination is not None):
            raise ValueError("normalized load/destination topology disagrees")
        if variant.modifier_value_availabilities:
            raise ValueError("tensor modifier-value views need explicit adapter support")
        dest_cpp = ("TensorKnownFactDestination::" +
                    destination.name.title() if destination is not None else
                    "std::nullopt")
        rows.append(
            "    {true, TensorKnownFactDirection::" + direction +
            ", TensorAccessMode::" + _CPP_TENSOR_MODE[variant.tensor_access_mode] +
            ", static_cast<TensorRank>(" + str(rank) + "), " + dest_cpp +
            ", " + str(variant.tensor_cta_group_applicable).lower() + "},"
        )
    return (_CPP_SELECTED_ADAPTER.replace("__META_ROWS__", "\n".join(rows))
            .replace("__META_COUNT__", str(len(rows))))


def _render_info_bounds() -> str:
    """Project accepted per-mode U16 bounds without owning another table."""

    names = {
        TensorAccessMode.IM2COL: "Im2col",
        TensorAccessMode.IM2COL_W: "Im2colW",
        TensorAccessMode.IM2COL_W128: "Im2colW128",
    }
    rows = []
    for mode, cpp_name in names.items():
        for rank in (3, 4, 5):
            bounds = [maximum for _, maximum in tensor_im2col_info_contract(mode, rank)]
            values = bounds + [0] * (3 - len(bounds))
            rows.append(
                "    {TensorAccessMode::" + cpp_name + ", " + str(rank) +
                ", " + str(len(bounds)) + ", {" +
                ", ".join(str(value) for value in values) + "}},"
            )
    return "\n".join([
        "/** Emitted from the accepted tensor_im2col_info_contract, not a second policy. */",
        "struct TensorKnownInfoBounds {",
        "  TensorAccessMode mode;",
        "  uint8_t rank;",
        "  uint8_t arity;",
        "  std::array<uint16_t, 3> maxima;",
        "};",
        "inline constexpr std::array<TensorKnownInfoBounds, 9> tensor_known_info_bounds{{",
        *rows,
        "}};",
    ])


_CPP_QUERY = r'''
/** Evaluate caller-supplied facts without inspecting descriptor bytes. */
TensorKnownFactsReport validate_tensor_access_facts(
    const TensorKnownAccessContext& c, const TensorMapKnownFacts& f) {
  using S = TensorKnownFactStatus;
  TensorKnownFactsReport report;
  const auto number = [](auto value) { return static_cast<uint8_t>(value); };
  const auto in_range = [&](auto value, uint8_t first, uint8_t last) {
    return number(value) >= first && number(value) <= last;
  };
  const auto rank = number(c.rank);
  const auto mode = c.mode;
  const bool gather = mode == TensorAccessMode::TileGather4 ||
                      mode == TensorAccessMode::TileScatter4;
  const bool tiled = mode == TensorAccessMode::Tiled || gather;
  const bool im2col = mode == TensorAccessMode::Im2col ||
                      mode == TensorAccessMode::Im2colW ||
                      mode == TensorAccessMode::Im2colW128;
  const bool w = mode == TensorAccessMode::Im2colW ||
                 mode == TensorAccessMode::Im2colW128;
  const auto packed = [](TensorMapElementType value) {
    return value == TensorMapElementType::B4x16 ||
           value == TensorMapElementType::B4x16P64 ||
           value == TensorMapElementType::B6x16P32OrB6p2x16;
  };
  const auto restricted = [](TensorMapElementType value) {
    return value == TensorMapElementType::B4x16P64 ||
           value == TensorMapElementType::B6x16P32OrB6p2x16;
  };
  const auto outcome = [&](uint8_t id, S status, std::string detail,
                           std::optional<uint64_t> value = std::nullopt) {
    return TensorKnownFactOutcome{id, status, std::move(detail), value};
  };
  const auto checked = [&](uint8_t id, std::string text,
                           std::optional<uint64_t> value = std::nullopt) {
    return outcome(id, S::Checked, std::move(text), value);
  };
  const auto violated = [&](uint8_t id, std::string text) {
    return outcome(id, S::Violated, std::move(text));
  };
  const auto unresolved = [&](uint8_t id, std::string text) {
    return outcome(id, S::Unresolved, std::move(text));
  };
  const auto na = [&](uint8_t id, std::string text) {
    return outcome(id, S::NotApplicable, std::move(text));
  };
  const auto valid_use = [](const TensorKnownConvertedInteger& value,
                            unsigned width, bool signed_use) {
    if (!value.value) return !value.source_bits;
    const int64_t minimum = signed_use ? -(INT64_C(1) << (width - 1)) : 0;
    const int64_t maximum = signed_use ? (INT64_C(1) << (width - 1)) - 1
                                       : (INT64_C(1) << width) - 1;
    if (*value.value < minimum || *value.value > maximum) return false;
    if (!value.source_bits) return true;
    const uint64_t mask = (UINT64_C(1) << width) - 1;
    uint64_t converted = *value.source_bits & mask;
    if (signed_use && (converted & (UINT64_C(1) << (width - 1))))
      return static_cast<int64_t>(converted) - (INT64_C(1) << width) ==
             *value.value;
    return static_cast<int64_t>(converted) == *value.value;
  };
  uint64_t damaged_mask = 0;
  bool global_context_damaged = false;
  const auto mark_damaged = [&](std::string_view name) {
    const auto input = std::find_if(tensor_map_fact_inputs.begin(),
        tensor_map_fact_inputs.end(), [&](const auto& entry) {
          return entry.name == name;
        });
    if (input != tensor_map_fact_inputs.end())
      damaged_mask |= input->bit;
  };
  const auto diagnose = [&](std::string_view name, std::string_view detail) {
    report.diagnostics.push_back(std::string(name) + ": " + std::string(detail));
    mark_damaged(name);
    if (name == "direction" || name == "mode" || name == "rank")
      global_context_damaged = true;
  };
  const auto decode = [&](const auto& claim, TensorMapReplaceField field,
                          std::string_view name) {
    using Value = typename std::remove_cvref_t<decltype(*claim)>::ValueType;
    if (!claim) return std::optional<Value>{};
    if (claim->valid == false) {
      report.diagnostics.push_back(std::string(name) + ": rejected projection");
      mark_damaged(name);
      return std::optional<Value>{};
    }
    if (!claim->valid) return std::optional<Value>{};
    if (!claim->code || !claim->value) {
      report.diagnostics.push_back(std::string(name) + ": missing code or typed value");
      mark_damaged(name);
      return std::optional<Value>{};
    }
    const ResolvedImmediate raw{.bits = *claim->code,
                                .type = ScalarType::B32,
                                .is_negative = false,
                                .integer_source_bits = *claim->code};
    const auto accepted = project_tensor_map_encoded_value<Value>(field, raw);
    if (!accepted || *accepted != *claim->value) {
      report.diagnostics.push_back(std::string(name) + ": code/value mismatch");
      mark_damaged(name);
      return std::optional<Value>{};
    }
    return accepted;
  };
  auto element = decode(f.element, TensorMapReplaceField::Elemtype, "element");
  const auto interleave = decode(f.interleave, TensorMapReplaceField::InterleaveLayout,
                                 "interleave");
  const auto swizzle = decode(f.swizzle, TensorMapReplaceField::SwizzleMode,
                              "swizzle");
  const auto atomicity = decode(f.atomicity, TensorMapReplaceField::SwizzleAtomicity,
                                "atomicity");
  const auto fill = decode(f.fill, TensorMapReplaceField::FillMode, "fill");
  const bool packed_family = element && packed(*element);
  bool code15_missing = false;
  bool code15_mismatch = false;
  if (f.element && f.element->code15_interpretation) {
    if (!f.element->code || *f.element->code != 15 ||
        number(*f.element->code15_interpretation) > 1) {
      report.diagnostics.emplace_back("element: invalid code-15 interpretation claim");
      mark_damaged("element");
    }
  }
  if (element && *element == TensorMapElementType::B6x16P32OrB6p2x16) {
    if (!f.element->code15_interpretation) code15_missing = true;
    else {
      const bool reading = c.direction == TensorKnownFactDirection::Load ||
                           c.direction == TensorKnownFactDirection::Prefetch;
      code15_mismatch = *f.element->code15_interpretation !=
          (reading ? TensorKnownCode15Interpretation::Load :
                     TensorKnownCode15Interpretation::Store);
    }
    if (code15_missing || code15_mismatch) element.reset();
  }
  const auto bad_array = [&](const auto& array, size_t maximum,
                             std::optional<size_t> expected = std::nullopt) {
    return array && (array->arity > maximum ||
                     (expected && array->arity != *expected));
  };
  if (!in_range(c.direction, 0, 3)) diagnose("direction", "invalid enum");
  if (!in_range(mode, 0, 6)) diagnose("mode", "invalid enum");
  if (rank < 1 || rank > 5) diagnose("rank", "invalid selected rank");
  if (c.destination && !in_range(*c.destination, 0, 1))
    diagnose("destination", "invalid enum");
  if (c.reduction_op && !in_range(*c.reduction_op, 0, 7))
    diagnose("reduction_op", "invalid enum");
  if (c.group && !in_range(*c.group, 0, 1))
    diagnose("group", "invalid enum");
  if (f.rank && (number(*f.rank) < 1 || number(*f.rank) > 5))
    diagnose("facts.rank", "invalid descriptor rank");
  if (f.rank && rank >= 1 && rank <= 5 && number(*f.rank) != rank)
    diagnose("facts.rank", "descriptor rank differs from selected rank");
  const auto fact_rank = f.rank ? number(*f.rank) : 0;
  if (bad_array(f.full_dimensions_elements, 5, fact_rank ? std::optional<size_t>(fact_rank) : std::nullopt))
    diagnose("full_dimensions_elements", "invalid supplied arity");
  if (bad_array(f.tiled_box_elements, 5, fact_rank ? std::optional<size_t>(fact_rank) : std::nullopt))
    diagnose("tiled_box_elements", "invalid supplied arity");
  if (bad_array(f.traversal_steps_elements, 5, fact_rank ? std::optional<size_t>(fact_rank) : std::nullopt))
    diagnose("traversal_steps_elements", "invalid supplied arity");
  if (bad_array(f.global_strides_bytes, 5, fact_rank ? std::optional<size_t>(fact_rank - 1) : std::nullopt))
    diagnose("global_strides_bytes", "invalid supplied arity");
  if (bad_array(f.tensor_strides_bytes, 5))
    diagnose("tensor_strides_bytes", "invalid supplied arity");
  if (bad_array(f.im2col_spatial_box_elements, 5, rank >= 3 && rank <= 5 ? std::optional<size_t>(rank - 2) : std::nullopt))
    diagnose("im2col_spatial_box_elements", "invalid supplied arity");
  if (bad_array(f.im2col_lower_edge_offsets, 3, rank >= 3 && rank <= 5 ? std::optional<size_t>(rank - 2) : std::nullopt))
    diagnose("im2col_lower_edge_offsets", "invalid supplied arity");
  if (bad_array(f.im2col_upper_edge_offsets, 3, rank >= 3 && rank <= 5 ? std::optional<size_t>(rank - 2) : std::nullopt))
    diagnose("im2col_upper_edge_offsets", "invalid supplied arity");
  if (c.coordinate_arity > 5 ||
      (c.coordinate_arity && c.coordinate_arity != (gather ? 5 : rank)))
    diagnose("coordinates", "invalid selected arity");
  for (size_t i = 0; i < std::min<size_t>(c.coordinate_arity, 5); ++i)
    if (!valid_use(c.coordinates[i], 32, true))
      diagnose("coordinates", "S32 source/use mismatch");
  const auto info_bounds = std::find_if(tensor_known_info_bounds.begin(),
      tensor_known_info_bounds.end(), [&](const auto& row) {
        return row.mode == mode && row.rank == rank;
      });
  if (c.info_arity > 3 || (c.info_known && im2col &&
      (info_bounds == tensor_known_info_bounds.end() ||
       c.info_arity != info_bounds->arity)))
    diagnose("info", "invalid selected arity");
  if (c.info_known && !im2col && c.info_arity)
    diagnose("info", "inapplicable mode");
  for (size_t i = 0; i < std::min<size_t>(c.info_arity, 3); ++i)
    if (!valid_use(c.info[i], 16, false))
      diagnose("info", "U16 source/use mismatch");
  if (c.active_atomicity_use && *c.active_atomicity_use && swizzle &&
      *swizzle == TensorMapSwizzleMode::None)
    diagnose("active_atomicity_use", "contradicts no-swizzle claim");
  const bool special = c.target_identity &&
      c.target_identity->source_spelling == "sm_103a" &&
      c.direction == TensorKnownFactDirection::Store && element &&
      *element == TensorMapElementType::B6x16P32OrB6p2x16;
  const auto geometry = [&](uint8_t id, bool override_profile) {
    if (!element) return unresolved(id, "element projection absent");
    if (!restricted(*element)) return na(id, "no packed geometry clause");
    const uint64_t required = override_profile ? 48 :
        (*element == TensorMapElementType::B4x16P64 ? 64 : 96);
    const uint64_t address_multiple = override_profile ? 16 : 32;
    const uint64_t coordinate_multiple = override_profile ? 64 : 128;
    if (f.inner_box_bytes && *f.inner_box_bytes != required &&
        !(override_profile && *f.inner_box_bytes == 96))
      return violated(id, "packed Box-Size[0] bytes conflict with profile");
    if (f.inner_tensor_bytes && *f.inner_tensor_bytes % required)
      return violated(id, "packed Tensor-Size[0] byte multiple violated");
    if (f.data_base_address_bytes && *f.data_base_address_bytes % address_multiple)
      return violated(id, "tensor data base byte alignment violated");
    if (f.tensor_strides_bytes)
      for (size_t i = 0; i < f.tensor_strides_bytes->arity; ++i)
        if (f.tensor_strides_bytes->values[i] &&
            *f.tensor_strides_bytes->values[i] % address_multiple)
          return violated(id, "tensor byte stride alignment violated");
    if (c.coordinate_arity && c.coordinates[0].value &&
        *c.coordinates[0].value % static_cast<int64_t>(coordinate_multiple))
      return violated(id, "converted first coordinate multiple violated");
    if (!f.inner_box_bytes || !f.inner_tensor_bytes || !f.data_base_address_bytes ||
        !f.tensor_strides_bytes || !c.coordinate_arity ||
        !c.coordinates[0].value)
      return unresolved(id, "packed byte or converted-coordinate fact absent");
    if (*f.inner_tensor_bytes == 0)
      return unresolved(id, "zero tensor extent has no sourced transfer-legality rule");
    for (size_t i = 0; i < f.tensor_strides_bytes->arity; ++i)
      if (!f.tensor_strides_bytes->values[i])
        return unresolved(id, "tensor byte stride fact absent");
      else if (*f.tensor_strides_bytes->values[i] == 0)
        return unresolved(id, "zero stride has no sourced transfer-legality rule");
    return checked(id, "supplied packed byte and coordinate facts satisfy profile");
  };
  for (const auto& row : tensor_map_fact_rules) {
    const uint8_t id = number(row.rule);
    if (global_context_damaged || (row.input_mask & damaged_mask)) {
      report.outcomes.push_back(unresolved(id,
          "malformed caller fact/context; see diagnostics"));
      continue;
    }
    TensorKnownFactOutcome item = unresolved(id, "relation not evaluated");
    switch (row.rule) {
      case TensorMapFactRule::ELEMENT_IDENTITY:
        item = code15_missing ? unresolved(id, "code 15 directional interpretation absent") :
            code15_mismatch ? violated(id, "code 15 interpretation conflicts with selected direction") :
            !element ? unresolved(id, "Table 33 element projection absent") :
            checked(id, "typed projected element and selected direction agree");
        break;
      case TensorMapFactRule::REDUCTION_TYPE: {
        if (c.direction != TensorKnownFactDirection::Reduce) {
          item = na(id, "not tensor reduction"); break;
        }
        if (!c.reduction_op || !f.reduction_interpretation) {
          item = unresolved(id, "reduction op or scalar interpretation absent"); break;
        }
        if (mode != TensorAccessMode::Tiled &&
            mode != TensorAccessMode::Im2colNoOffs) {
          item = violated(id, "unsupported tensor-reduction mode"); break;
        }
        if (!tensor_reduction_accepts_element_type(*c.reduction_op,
                                                   *f.reduction_interpretation)) {
          item = violated(id, "accepted operation/type matrix rejects interpretation"); break;
        }
        if (!element) { item = unresolved(id, "descriptor format absent"); break; }
        if (*element == TensorMapElementType::F32Ftz ||
            *element == TensorMapElementType::TF32 ||
            *element == TensorMapElementType::TF32Ftz ||
            *f.reduction_interpretation == base::ScalarType::B32 ||
            *f.reduction_interpretation == base::ScalarType::B64) {
          item = unresolved(id, "FTZ/TF32/bit-type bridge not sourced"); break;
        }
        std::optional<base::ScalarType> exact;
        switch (*element) {
          case TensorMapElementType::U32: exact = base::ScalarType::U32; break;
          case TensorMapElementType::S32: exact = base::ScalarType::S32; break;
          case TensorMapElementType::U64: exact = base::ScalarType::U64; break;
          case TensorMapElementType::S64: exact = base::ScalarType::S64; break;
          case TensorMapElementType::F16: exact = base::ScalarType::F16; break;
          case TensorMapElementType::F32: exact = base::ScalarType::F32; break;
          case TensorMapElementType::BF16: exact = base::ScalarType::BF16; break;
          default: break;
        }
        item = !exact ? unresolved(id, "descriptor/reduction identity bridge not sourced") :
            (*exact == *f.reduction_interpretation ?
             checked(id, "accepted operation/type and exact identity agree") :
             violated(id, "exact named descriptor/reduction identities differ"));
        break;
      }
      case TensorMapFactRule::BASE_BOX:
        if (!tiled) item = na(id, "not generic tiled box");
        else if (f.accessed_box_bytes && *f.accessed_box_bytes % 16)
          item = violated(id, "accessed box bytes not a 16-byte multiple");
        else if (f.accessed_box_address_bytes && *f.accessed_box_address_bytes % 16)
          item = violated(id, "accessed box address not 16-byte aligned");
        else if (!f.accessed_box_bytes || !f.accessed_box_address_bytes)
          item = unresolved(id, "box byte size or accessed address absent");
        else if (*f.accessed_box_bytes == 0)
          item = unresolved(id, "zero box bytes have no sourced nonempty-transfer legality");
        else item = checked(id, "supplied box bytes/address satisfy 16-byte rules");
        break;
      case TensorMapFactRule::TILED_TRAVERSAL:
        if (!tiled) item = na(id, "not tiled traversal");
        else if (!interleave || !f.traversal_steps_elements ||
                 !f.traversal_steps_elements->arity ||
                 !f.traversal_steps_elements->values[0])
          item = unresolved(id, "interleave or first traversal step absent");
        else if (*interleave == TensorMapInterleaveLayout::None &&
                 *f.traversal_steps_elements->values[0] != 1)
          item = violated(id, "noninterleaved first element step must be one");
        else if (*interleave == TensorMapInterleaveLayout::None)
          item = checked(id, "noninterleaved first element step is one");
        else item = unresolved(id, "general interleaved traversal predicate not sourced");
        break;
      case TensorMapFactRule::FILL_PACKED:
        if ((!element && !packed_family) || !fill) item = unresolved(id, "format or fill absent");
        else if (packed_family && *fill == TensorMapFillMode::OobNan)
          item = violated(id, "OOB-NaN fill excludes packed formats");
        else if (packed_family) item = checked(id, "packed format avoids OOB-NaN fill");
        else item = unresolved(id, "nonpacked fill/type matrix not fully sourced");
        break;
      case TensorMapFactRule::SUBBYTE_OPERATION:
        if (!element && !packed_family) item = unresolved(id, "element absent");
        else if (!packed_family) item = na(id, "not packed");
        else if (c.direction == TensorKnownFactDirection::Reduce)
          item = violated(id, "reduction excludes packed formats");
        else if (!element) item = unresolved(id, "code-15 directional subtype absent");
        else if (c.direction == TensorKnownFactDirection::Store &&
                 *element == TensorMapElementType::B4x16P64)
          item = violated(id, "store excludes b4x16_p64");
        else if (c.direction == TensorKnownFactDirection::Load &&
                 c.destination == TensorKnownFactDestination::Cluster &&
                 c.target_identity && c.target_identity->source_spelling == "sm_120a")
          item = violated(id, "sm120a cluster excludes packed format");
        else item = checked(id, "no selected packed-direction exclusion");
        break;
      case TensorMapFactRule::PACKED_GEOMETRY:
        item = special ? na(id, "exact sm103a store override applies") :
                         geometry(id, false);
        break;
      case TensorMapFactRule::PACKED_SWIZZLE:
        if (special) item = na(id, "exact sm103a store override applies");
        else if (!element) item = unresolved(id, "element absent");
        else if (!restricted(*element)) item = na(id, "no restricted packed swizzle");
        else if (!swizzle) item = unresolved(id, "swizzle absent");
        else if (*swizzle != TensorMapSwizzleMode::None &&
                 *swizzle != TensorMapSwizzleMode::Bytes128)
          item = violated(id, "packed swizzle must be none or 128 bytes");
        else if (*swizzle == TensorMapSwizzleMode::Bytes128 && !atomicity)
          item = unresolved(id, "128-byte packed atomicity absent");
        else if (*swizzle == TensorMapSwizzleMode::Bytes128 &&
                 *atomicity == TensorMapSwizzleAtomicity::Bytes32Flip8)
          item = violated(id, "packed format excludes eight-byte flip");
        else item = checked(id, "packed swizzle satisfies generic clause");
        break;
      case TensorMapFactRule::INTERLEAVE:
        if (!interleave) item = unresolved(id, "interleave absent");
        else if (*interleave == TensorMapInterleaveLayout::None)
          item = checked(id, "no interleave selected");
        else if (rank < 3 || gather || w)
          item = violated(id, "interleave excludes this rank/mode");
        else item = unresolved(id, "channel-slice conversion not sourced");
        break;
      case TensorMapFactRule::SWIZZLE_ATOMICITY:
        if (!swizzle) item = unresolved(id, "swizzle absent");
        else if (*swizzle == TensorMapSwizzleMode::None)
          item = na(id, "atomicity not applicable without swizzle");
        else if (!c.active_atomicity_use)
          item = unresolved(id, "selected atomicity use provenance absent");
        else if (!*c.active_atomicity_use) item = na(id, "selected use absent");
        else if (!atomicity) item = unresolved(id, "active atomicity absent");
        else if ((*swizzle == TensorMapSwizzleMode::Bytes32 ||
                  *swizzle == TensorMapSwizzleMode::Bytes64 ||
                  *swizzle == TensorMapSwizzleMode::Bytes96) &&
                 *atomicity != TensorMapSwizzleAtomicity::Bytes16)
          item = violated(id, "swizzle requires 16-byte atomicity");
        else item = checked(id, "selected swizzle/atomicity pair satisfies Table 14");
        break;
      case TensorMapFactRule::SWIZZLE_ALIGNMENT:
        if (!swizzle) item = unresolved(id, "swizzle absent");
        else if (*swizzle != TensorMapSwizzleMode::Bytes128)
          item = na(id, "extra alignment applies only to 128-byte swizzle");
        else if (c.active_atomicity_use != true)
          item = unresolved(id, "selected atomicity use not established");
        else if (!atomicity) item = unresolved(id, "atomicity absent");
        else if (*atomicity != TensorMapSwizzleAtomicity::Bytes32 &&
                 *atomicity != TensorMapSwizzleAtomicity::Bytes64)
          item = na(id, "no extra alignment for this atomicity");
        else if (!f.shared_destination_address_bytes)
          item = unresolved(id, "shared destination address absent");
        else if (*f.shared_destination_address_bytes %
                 (*atomicity == TensorMapSwizzleAtomicity::Bytes32 ? 32 : 64))
          item = violated(id, "shared destination lacks selected alignment");
        else item = checked(id, "shared destination meets selected atomicity alignment");
        break;
      case TensorMapFactRule::PATTERN_BASE_OFFSET: {
        if (!swizzle) { item = unresolved(id, "swizzle absent"); break; }
        if (*swizzle == TensorMapSwizzleMode::None) {
          item = na(id, "no repeating swizzle pattern"); break;
        }
        if (!f.shared_destination_address_bytes) {
          item = unresolved(id, "shared destination address absent"); break;
        }
        const uint64_t modulus = *swizzle == TensorMapSwizzleMode::Bytes64 ? 4 :
            (*swizzle == TensorMapSwizzleMode::Bytes128 ? 8 : 2);
        item = checked(id, "repeating-pattern offset computed, not alignment gate",
                       (*f.shared_destination_address_bytes / 128) % modulus);
        break;
      }
      case TensorMapFactRule::SWIZZLE_96:
        if (!swizzle) item = unresolved(id, "swizzle absent");
        else if (*swizzle != TensorMapSwizzleMode::Bytes96)
          item = na(id, "not 96-byte swizzle");
        else if (mode == TensorAccessMode::Im2colW128 ||
                 (element && restricted(*element)))
          item = violated(id, "96-byte swizzle excludes W128/packed profile");
        else if (interleave && *interleave != TensorMapInterleaveLayout::None)
          item = violated(id, "96-byte swizzle excludes interleave");
        else if (c.active_atomicity_use == true && atomicity &&
                 *atomicity != TensorMapSwizzleAtomicity::Bytes16)
          item = violated(id, "96-byte swizzle requires 16-byte atomicity");
        else if (f.inner_box_bytes && *f.inner_box_bytes > 96)
          item = violated(id, "96-byte swizzle box exceeds 96 bytes");
        else if (!interleave || !f.inner_box_bytes || !element ||
                 !c.active_atomicity_use ||
                 (*c.active_atomicity_use && !atomicity))
          item = unresolved(id, "interleave/box/element/selected use absent");
        else item = checked(id, "known 96-byte swizzle conditions satisfied");
        break;
      case TensorMapFactRule::FLIP_8:
        if (!swizzle || !atomicity)
          item = unresolved(id, "swizzle or atomicity absent");
        else if (*swizzle != TensorMapSwizzleMode::Bytes128 ||
                 *atomicity != TensorMapSwizzleAtomicity::Bytes32Flip8)
          item = na(id, "not 128-byte swizzle with eight-byte flip");
        else if (!c.active_atomicity_use)
          item = unresolved(id, "active selected use absent");
        else if (!*c.active_atomicity_use) item = na(id, "selected use absent");
        else if (c.direction != TensorKnownFactDirection::Load || w)
          item = violated(id, "eight-byte flip requires load and excludes W");
        else item = checked(id, "selected eight-byte-flip load allowed");
        break;
      case TensorMapFactRule::SM103A_PACKED_STORE:
        if (!c.target_identity) item = unresolved(id, "exact target identity absent");
        else if (!element) item = unresolved(id, "element projection absent");
        else if (!special) item = na(id, "not exact sm103a B6p2 tensor store");
        else {
          item = geometry(id, true);
          if (item.status == S::Violated) break;
          if (swizzle && *swizzle != TensorMapSwizzleMode::None &&
              *swizzle != TensorMapSwizzleMode::Bytes64 &&
              *swizzle != TensorMapSwizzleMode::Bytes128)
            item = violated(id, "exact packed store excludes this swizzle");
          else if (swizzle && *swizzle == TensorMapSwizzleMode::Bytes64 &&
                   c.active_atomicity_use == true && atomicity &&
                   *atomicity != TensorMapSwizzleAtomicity::Bytes16)
            item = violated(id, "64-byte override swizzle requires 16-byte atomicity");
          else if (swizzle && *swizzle == TensorMapSwizzleMode::Bytes128 &&
                   c.active_atomicity_use == true && atomicity &&
                   *atomicity == TensorMapSwizzleAtomicity::Bytes32Flip8)
            item = violated(id, "override excludes 128-byte eight-byte flip");
          else if (item.status == S::Unresolved || !swizzle ||
                   !c.active_atomicity_use ||
                   (*swizzle != TensorMapSwizzleMode::None &&
                    *c.active_atomicity_use && !atomicity))
            item = unresolved(id, "override bytes or selected swizzle/atomicity absent");
          else item = checked(id, "exact sm103a packed-store override satisfied");
        }
        break;
      case TensorMapFactRule::SM120A_CLUSTER:
        if (!c.target_identity) item = unresolved(id, "exact target identity absent");
        else if (c.target_identity->source_spelling != "sm_120a" ||
                 c.direction != TensorKnownFactDirection::Load ||
                 c.destination != TensorKnownFactDestination::Cluster)
          item = na(id, "not exact sm120a cluster tensor load");
        else if (packed_family)
          item = violated(id, "exact target excludes packed cluster load");
        else if (c.active_atomicity_use == true)
          item = violated(id, "exact target excludes applied atomicity");
        else if (!element || !c.active_atomicity_use)
          item = unresolved(id, "element or active-use provenance absent");
        else item = checked(id, "known exact-target exclusions absent");
        break;
      case TensorMapFactRule::STORE_BOUNDS: {
        if (c.direction != TensorKnownFactDirection::Store &&
            c.direction != TensorKnownFactDirection::Reduce) {
          item = na(id, "not tensor write"); break;
        }
        bool negative = false;
        bool unknown = c.coordinate_arity == 0;
        for (size_t i = 0; i < c.coordinate_arity; ++i) {
          if (!c.coordinates[i].value) unknown = true;
          else if (*c.coordinates[i].value < 0) negative = true;
        }
        if (negative) { item = violated(id, "converted S32 write coordinate negative"); break; }
        if (f.im2col_lower_edge_offsets)
          for (size_t i = 0; i < f.im2col_lower_edge_offsets->arity; ++i)
            if (f.im2col_lower_edge_offsets->values[i] &&
                *f.im2col_lower_edge_offsets->values[i] < 0) negative = true;
        if (negative) { item = violated(id, "store lower-edge offset negative"); break; }
        if (f.im2col_upper_edge_offsets)
          for (size_t i = 0; i < f.im2col_upper_edge_offsets->arity; ++i)
            if (f.im2col_upper_edge_offsets->values[i] &&
                *f.im2col_upper_edge_offsets->values[i] > 0) negative = true;
        item = negative ? violated(id, "store upper-edge offset positive") :
            unresolved(id, unknown ? "runtime coordinate unknown" :
                       "box-within-tensor relation lacks sourced opposite-edge transform");
        break;
      }
      case TensorMapFactRule::IM2COL_SHAPE: {
        if (!im2col && mode != TensorAccessMode::Im2colNoOffs) {
          item = na(id, "not im2col geometry"); break;
        }
        if (rank < 3 || rank > 5) {
          item = violated(id, "im2col requires rank three through five"); break;
        }
        const int32_t limit = rank == 3 ? 32768 : (rank == 4 ? 128 : 16);
        bool out_of_range = false;
        for (const auto* array : {&f.im2col_lower_edge_offsets,
                                  &f.im2col_upper_edge_offsets})
          if (*array)
            for (size_t i = 0; i < (*array)->arity; ++i)
              if ((*array)->values[i] &&
                  (*(*array)->values[i] < -limit ||
                   *(*array)->values[i] >= limit)) out_of_range = true;
        if (out_of_range) item = violated(id, "rank-specific opposite-edge range violated");
        else if (!f.im2col_spatial_box_elements ||
                 !f.im2col_lower_edge_offsets || !f.im2col_upper_edge_offsets)
          item = unresolved(id, "spatial box or opposite-edge offsets absent");
        else item = unresolved(id, "filter-base-to-box relation lacks selected spatial coordinate provenance");
        break;
      }
      case TensorMapFactRule::IM2COL_INFO:
      case TensorMapFactRule::W_PROFILE: {
        const bool profile_w = row.rule == TensorMapFactRule::W_PROFILE;
        if (profile_w ? !w : mode != TensorAccessMode::Im2col) {
          item = na(id, "selected mode has no such information profile"); break;
        }
        if (rank < 3 || rank > 5 || info_bounds == tensor_known_info_bounds.end()) {
          item = violated(id, "selected im2col information mode/rank invalid"); break;
        }
        if (profile_w) {
          if (interleave && *interleave != TensorMapInterleaveLayout::None) {
            item = violated(id, "W profile excludes interleave"); break;
          }
          if (swizzle && *swizzle == TensorMapSwizzleMode::None) {
            item = violated(id, "W profile requires swizzle"); break;
          }
          if (swizzle && *swizzle == TensorMapSwizzleMode::Bytes128 &&
              atomicity &&
              *atomicity == TensorMapSwizzleAtomicity::Bytes32Flip8 &&
              c.active_atomicity_use == true) {
            item = violated(id, "W profile excludes applied eight-byte flip");
            break;
          }
          if (f.im2col_spatial_box_elements)
            for (size_t i = 0; i + 1 < f.im2col_spatial_box_elements->arity; ++i)
              if (f.im2col_spatial_box_elements->values[i] &&
                  *f.im2col_spatial_box_elements->values[i] != 1) {
                item = violated(id, "W fixes D/H spatial extents to one"); break;
              }
          if (item.status == S::Violated) break;
        }
        if (!c.info_known || !c.info_arity) {
          item = unresolved(id, "optional information omitted or unknown"); break;
        }
        if (c.info_arity != info_bounds->arity) {
          item = violated(id, "information count differs from profile"); break;
        }
        bool bad_info = false;
        bool unknown_info = false;
        for (size_t i = 0; i < c.info_arity; ++i) {
          if (!c.info[i].value) unknown_info = true;
          else if (*c.info[i].value > info_bounds->maxima[i]) bad_info = true;
        }
        if (bad_info) item = violated(id, "converted U16 information exceeds profile bound");
        else if (unknown_info) item = unresolved(id, "runtime U16 information unknown");
        else item = unresolved(id, profile_w ?
            "W width/opposite-edge relationship not fully sourced" :
            "pixel/traversal/OOB relationship not fully sourced");
        break;
      }
      case TensorMapFactRule::GATHER_SCATTER:
        if (!gather) item = na(id, "not gather4/scatter4");
        else if (rank != 2 || (c.coordinate_arity && c.coordinate_arity != 5))
          item = violated(id, "four-row mode requires rank two and five roles");
        else if (interleave && *interleave != TensorMapInterleaveLayout::None)
          item = violated(id, "four-row mode excludes interleave");
        else if (f.tiled_box_elements && f.tiled_box_elements->arity > 1 &&
                 f.tiled_box_elements->values[1] &&
                 *f.tiled_box_elements->values[1] != 1)
          item = violated(id, "four-row box dimension one must equal one");
        else item = unresolved(id, "per-row boundary extents not independently known");
        break;
      case TensorMapFactRule::SELECTED_AVAILABILITY:
        if (c.selected_variant_available == false ||
            c.selected_value_available == false)
          item = violated(id, "canonical selected availability query rejects target");
        else if (!c.selected_variant_available || !c.selected_value_available)
          item = unresolved(id, "canonical selected availability query absent");
        else item = checked(id, "canonical selected availability queries accepted target");
        break;
    }
    report.outcomes.push_back(std::move(item));
  }
  return report;
}
'''


_CPP_SELECTED_ADAPTER = r'''
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>

#include <variant>

namespace ptx_frontend::resolved_ir {
namespace {

/** Closed normalized selected-form metadata; never parsed from variant names. */
struct TensorKnownSelectedMeta {
  bool is_tensor;
  TensorKnownFactDirection direction;
  TensorAccessMode mode;
  TensorRank rank;
  std::optional<TensorKnownFactDestination> destination;
  bool group_applicable;
};

/** One row per generated Cp alternative in the checker's descriptor order. */
inline constexpr std::array<TensorKnownSelectedMeta, __META_COUNT__>
    tensor_known_selected_meta{{
__META_ROWS__
    }};

}  // namespace

/** Copy selected tensor facts while every borrowed checker view is in scope. */
TensorKnownSelectedProjection project_tensor_known_access_context(
    const Cp& instruction, const checker::Context& checker_context) {
  TensorKnownSelectedProjection result;
  if (instruction.variant.valueless_by_exception()) {
    result.diagnostics.emplace_back("selected Cp variant storage is valueless");
    return result;
  }
  const size_t index = instruction.variant.index();
  const auto descriptors = Cp::get_checker_descriptor().variants;
  if (index >= tensor_known_selected_meta.size() || index >= descriptors.size()) {
    result.diagnostics.emplace_back("selected Cp variant lacks canonical descriptor");
    return result;
  }
  const auto& meta = tensor_known_selected_meta[index];
  if (!meta.is_tensor) return result;
  const auto& descriptor = descriptors[index];
  TensorKnownAccessContext access;
  access.direction = meta.direction;
  access.mode = meta.mode;
  access.rank = meta.rank;
  access.destination = meta.destination;
  access.target_identity = checker_context.target.identity;
  bool copied = false;
  const auto copy_tensor = [&](const ResolvedTensorOperand& tensor,
                               const ResolvedTensorIm2colInfo* info) {
    copied = true;
    if (tensor.rank != meta.rank || tensor.mode != meta.mode) {
      result.diagnostics.emplace_back("selected tensor rank/mode disagrees with normalized variant");
      return;
    }
    const size_t expected_coordinates =
        meta.mode == TensorAccessMode::TileGather4 ||
                meta.mode == TensorAccessMode::TileScatter4
            ? 5 : static_cast<size_t>(meta.rank);
    if (tensor.coordinates.elements.size() != expected_coordinates ||
        expected_coordinates > access.coordinates.size()) {
      result.diagnostics.emplace_back("selected tensor coordinate arity is malformed");
      return;
    }
    access.coordinate_arity = expected_coordinates;
    for (size_t i = 0; i < expected_coordinates; ++i) {
      if ((meta.mode == TensorAccessMode::TileGather4 ||
           meta.mode == TensorAccessMode::TileScatter4) &&
          !tensor_gather_scatter_coordinate_role(tensor, i)) {
        result.diagnostics.emplace_back("selected gather/scatter coordinate role is malformed");
        return;
      }
      const auto* immediate = std::get_if<ResolvedImmediate>(
          &tensor.coordinates.elements[i]);
      if (!immediate) continue;
      if (immediate->type != ScalarType::S32) {
        result.diagnostics.emplace_back("selected coordinate lacks S32 use type");
        return;
      }
      const uint32_t bits = static_cast<uint32_t>(immediate->bits);
      const int64_t converted = bits & UINT32_C(0x80000000)
          ? static_cast<int64_t>(bits) - INT64_C(4294967296)
          : static_cast<int64_t>(bits);
      access.coordinates[i].value = converted;
      access.coordinates[i].source_bits = immediate->integer_source_bits;
      if (immediate->integer_source_bits &&
          static_cast<uint32_t>(*immediate->integer_source_bits) != bits) {
        result.diagnostics.emplace_back("selected coordinate source/use bits disagree");
        return;
      }
    }
    if (!info) return;
    access.info_known = true;
    if (info->elements.size() > access.info.size()) {
      result.diagnostics.emplace_back("selected im2col information arity exceeds bound");
      return;
    }
    access.info_arity = info->elements.size();
    for (size_t i = 0; i < access.info_arity; ++i) {
      if (!tensor_im2col_info_role(tensor, *info, i)) {
        result.diagnostics.emplace_back("selected im2col information role is malformed");
        return;
      }
      const auto* immediate = std::get_if<ResolvedImmediate>(&info->elements[i]);
      if (!immediate) continue;
      if (immediate->type != ScalarType::U16) {
        result.diagnostics.emplace_back("selected im2col information lacks U16 use type");
        return;
      }
      const uint16_t bits = static_cast<uint16_t>(immediate->bits);
      access.info[i].value = bits;
      access.info[i].source_bits = immediate->integer_source_bits;
      if (immediate->integer_source_bits &&
          static_cast<uint16_t>(*immediate->integer_source_bits) != bits) {
        result.diagnostics.emplace_back("selected information source/use bits disagree");
        return;
      }
    }
  };
  std::visit([&](const auto& selected) {
    using Selected = std::remove_cvref_t<decltype(selected)>;
    if (selected.operand_layout.value >= descriptor.operand_layouts.size()) {
      result.diagnostics.emplace_back("selected tensor operand-layout tag is out of range");
      return;
    }
    if constexpr (requires { selected.operands; }) {
      if (selected.operands.valueless_by_exception() ||
          selected.operand_layout.value != selected.operands.index()) {
        result.diagnostics.emplace_back("selected tensor layout tag/storage disagree");
        return;
      }
      std::visit([&](const auto& payload) {
        if constexpr (requires { payload.tensor.value; }) {
          using Tensor = std::remove_cvref_t<decltype(payload.tensor.value)>;
          if constexpr (std::same_as<Tensor, ResolvedTensorOperand>) {
            if constexpr (requires { payload.im2col_info.value; })
              copy_tensor(payload.tensor.value, &payload.im2col_info.value);
            else
              copy_tensor(payload.tensor.value, nullptr);
          }
        }
      }, selected.operands);
    } else if constexpr (requires { selected.tensor.value; }) {
      using Tensor = std::remove_cvref_t<decltype(selected.tensor.value)>;
      if constexpr (std::same_as<Tensor, ResolvedTensorOperand>)
        copy_tensor(selected.tensor.value, nullptr);
    }
    if constexpr (requires { Selected::tensor_reduction_op; })
      access.reduction_op = Selected::tensor_reduction_op;
    if constexpr (requires { selected.tensor_cta_group_role(); }) {
      if (meta.group_applicable) {
        const auto role = selected.tensor_cta_group_role();
        if (!role)
          result.diagnostics.emplace_back("selected tensor CTA-group role is malformed");
        else
          access.group = role->effective;
      }
    }
  }, instruction.variant);
  if (!copied) result.diagnostics.emplace_back("selected tensor operand storage is absent");
  if (!result.diagnostics.empty()) return result;
  access.selected_variant_available =
      checker::check_availability(descriptor, checker_context).has_value();
  access.selected_value_available =
      checker::check_operand_layout_availability(
          descriptor, std::visit([](const auto& selected) {
            return selected.operand_layout.value;
          }, instruction.variant), checker_context).has_value() &&
      checker::check_modifier_value_availability(
          descriptor.modifier_value_availabilities,
          std::span<const checker::ModifierValueView>{}, checker_context).has_value();
  result.access = std::move(access);
  return result;
}

}  // namespace ptx_frontend::resolved_ir
'''
