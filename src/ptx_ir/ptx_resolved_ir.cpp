#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <string_view>
#include <vector>
#include "syntax_descriptor.gen.hpp"

namespace ptx_frontend::resolved_ir::check_end {

bool ModifierDescriptor::check(std::string modifier_str) const {
  if (this->presence == PresenceRequirement::Absent) {
    return std::ranges::find(this->allowed_values, modifier_str) ==
           this->allowed_values.end();
  } else if (this->presence == PresenceRequirement::Optional) {
    return true;  // any value is allowed
  } else if (this->presence == PresenceRequirement::Required) {
    return std::ranges::find(this->allowed_values, modifier_str) !=
           this->allowed_values.end();
  }
  return false;  // should not reach here
}

int32_t VariantDescriptor::get_required_modifier_num() const {
  int32_t size = 0;
  for (const auto& item : this->modifiers) {
    if (item.presence == PresenceRequirement::Absent or
        item.presence == PresenceRequirement::Required) {
      size += 1;
    }
  }
  return size;
}

};  // namespace ptx_frontend::resolved_ir::check_end

namespace ptx_frontend::resolved_ir {
namespace {

using check_end::OperandLayoutDescriptor;
using check_end::OperandPresence;
using check_end::OperandSlotDescriptor;
using check_end::OperandSyntaxShape;
using check_end::ResolvedValueKind;
using check_end::VariantDescriptor;

/**
 * @brief Check if the actual operand syntax shape is allowed by the allowed shape.
 * 
 * @param allowed allowed operand syntax shape 
 * @param actual actual operand syntax shape
 * @return true if the actual shape is allowed by the allowed shape
 * @return false otherwise
 */
bool allows_shape(OperandSyntaxShape allowed, OperandSyntaxShape actual) {
  using Underlying = std::underlying_type_t<OperandSyntaxShape>;
  return (static_cast<Underlying>(allowed) & static_cast<Underlying>(actual)) !=
         0;
}

/**
 * @brief Check if the actual operands of an instruction match the operand layout descriptor.
 * 
 * @param layout operand layout descriptor
 * @param ast syntax AST instruction
 * @return true if the actual operands match the layout descriptor
 * @return false otherwise
 */
bool matches_operand_layout(const OperandLayoutDescriptor& layout,
                            const syntax_ast::AstInstruction& ast) {
  if (ast.operands.size() > layout.slots.size())
    return false;

  for (size_t index = 0; index < layout.slots.size(); ++index) {
    const OperandSlotDescriptor& slot = layout.slots[index];
    if (index == ast.operands.size()) {
      if (slot.presence == OperandPresence::Required)
        return false;
      continue;
    }

    if (!allows_shape(slot.allowed_shapes, check_end::get_operand_syntax_shape(
                                               ast.operands[index]))) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Select the operand layout descriptor that matches the actual operands of an instruction.
 * 
 * @param variant instruction variant descriptor
 * @param ast syntax AST instruction
 * @return std::expected<const OperandLayoutDescriptor*, ResolveDiagnostic> 
 */
std::expected<const OperandLayoutDescriptor*, ResolveDiagnostic>
select_operand_layout(const VariantDescriptor& variant,
                      const syntax_ast::AstInstruction& ast) {
  const OperandLayoutDescriptor* selected = nullptr;
  for (const OperandLayoutDescriptor& layout : variant.operand_layouts) {
    if (!matches_operand_layout(layout, ast))
      continue;
    if (selected != nullptr) {
      throw ResolveException(fmt::format(
          "Descriptor variant '{}': multiple operand layouts match one "
          "syntax instruction.",
          variant.variant_name));
    }
    selected = &layout;
  }

  if (selected != nullptr)
    return selected;
  return std::unexpected(ResolveDiagnostic{
      .range = ast.range,
      .message = fmt::format(
          "Operands do not match any layout of instruction variant '{}'.",
          variant.variant_name),
  });
}

/**
 * @brief Find the variant descriptor of an instruction by its name.
 * 
 * @param instruction instruction descriptor
 * @param name variant name string
 * @return const VariantDescriptor& 
 */
const VariantDescriptor& find_variant_descriptor(
    const check_end::InstructionDescriptor& instruction,
    std::string_view name) {
  const auto& descriptors = instruction.variants;
  const auto it = std::ranges::find_if(
      descriptors, [name](const VariantDescriptor& descriptor) {
        return descriptor.variant_name == name;
      });
  if (it == descriptors.end()) {
    throw ResolveException(
        fmt::format("Descriptor for '{}' has no variant named '{}'.",
                    instruction.Opcode_name, name));
  }
  return *it;
}

std::expected<WithLocs<ScalarType>, ResolveDiagnostic> resolve_scalar_type(
    const syntax_ast::AstModifier& modifier) {
  static constexpr std::array<std::pair<std::string_view, ScalarType>, 30>
      scalar_types = {{
          {".u8", ScalarType::U8},         {".u8x4", ScalarType::U8x4},
          {".u16", ScalarType::U16},       {".u16x2", ScalarType::U16x2},
          {".u32", ScalarType::U32},       {".u64", ScalarType::U64},
          {".s8", ScalarType::S8},         {".s8x4", ScalarType::S8x4},
          {".s16", ScalarType::S16},       {".s16x2", ScalarType::S16x2},
          {".s32", ScalarType::S32},       {".s64", ScalarType::S64},
          {".b8", ScalarType::B8},         {".b16", ScalarType::B16},
          {".b32", ScalarType::B32},       {".b64", ScalarType::B64},
          {".b128", ScalarType::B128},     {".f16", ScalarType::F16},
          {".f16x2", ScalarType::F16x2},   {".f32", ScalarType::F32},
          {".f32x2", ScalarType::F32x2},   {".f64", ScalarType::F64},
          {".bf16", ScalarType::BF16},     {".bf16x2", ScalarType::BF16x2},
          {".e4m3x2", ScalarType::E4m3x2}, {".e5m2x2", ScalarType::E5m2x2},
          {".pred", ScalarType::Pred},     {".tf32", ScalarType::TF32},
          {".e4m3", ScalarType::E4m3},     {".e5m2", ScalarType::E5m2},
      }};

  const auto it =
      std::ranges::find_if(scalar_types, [&modifier](const auto& entry) {
        return entry.first == modifier.syntax.text;
      });
  if (it == scalar_types.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown scalar type '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<ScalarType>{it->second, modifier.syntax.range};
}

std::expected<WithLocs<ResolvedRegisterId>, ResolveDiagnostic> resolve_register(
    const syntax_ast::AstOperand& operand) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = std::visit([](const auto& item) { return item.syntax.range; },
                            operand),
        .message = "Expected a register operand.",
    });
  }

  const std::string_view spelling = identifier->syntax.text;
  size_t digit_begin = spelling.size();
  while (digit_begin > 0 &&
         std::isdigit(static_cast<unsigned char>(spelling[digit_begin - 1]))) {
    --digit_begin;
  }
  if (spelling.size() < 3 || spelling.front() != '%' ||
      digit_begin == spelling.size() || digit_begin == 1) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message =
            fmt::format("Expected a numbered register, got '{}'.", spelling),
    });
  }

  uint32_t value = 0;
  const char* first = spelling.data() + digit_begin;
  const char* last = spelling.data() + spelling.size();
  const auto [end, error] = std::from_chars(first, last, value);
  if (error != std::errc{} || end != last) {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message =
            fmt::format("Register '{}' has an invalid numeric ID.", spelling),
    });
  }
  return WithLocs<ResolvedRegisterId>{ResolvedRegisterId{value},
                                      identifier->syntax.range};
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstImmediate& immediate, ScalarType type) {
  std::string_view text = immediate.syntax.text;
  bool negative = false;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }

  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    base = 16;
    text.remove_prefix(2);
  } else if (text.starts_with("0f") || text.starts_with("0F") ||
             text.starts_with("0d") || text.starts_with("0D")) {
    base = 16;
    text.remove_prefix(2);
  }

  uint64_t bits = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), bits, base);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size()) {
    return std::unexpected(ResolveDiagnostic{
        .range = immediate.syntax.range,
        .message = fmt::format("Unsupported immediate literal '{}'.",
                               immediate.syntax.text),
    });
  }
  if (negative)
    bits = uint64_t{0} - bits;
  return ResolvedImmediate{.bits = bits, .type = type};
}

std::expected<WithLocs<RegOrImm>, ResolveDiagnostic> resolve_reg_or_imm(
    const syntax_ast::AstOperand& operand, ScalarType type) {
  if (const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    auto register_id = resolve_register(operand);
    if (!register_id)
      return std::unexpected(register_id.error());
    return WithLocs<RegOrImm>{RegOrImm{register_id->value},
                              identifier->syntax.range};
  }
  if (const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand)) {
    auto value = resolve_immediate_value(*immediate, type);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<RegOrImm>{RegOrImm{*value}, immediate->syntax.range};
  }
  return std::unexpected(ResolveDiagnostic{
      .range = std::visit([](const auto& item) { return item.syntax.range; },
                          operand),
      .message = "Expected a register or immediate operand.",
  });
}

/**
 * @brief Check if the actual modifiers of an instruction match the variant descriptor.
 * 
 * @param instruction instruction descriptor
 * @param spelling actual modifier spelling
 * @return std::optional<std::string_view> kind ID of the modifier if it matches, std::nullopt otherwise
 */
std::optional<std::string_view> modifier_kind_id(
    const check_end::InstructionDescriptor& instruction,
    std::string_view spelling) {
  std::optional<std::string_view> result;
  for (const VariantDescriptor& variant : instruction.variants) {
    for (const check_end::ModifierDescriptor& modifier : variant.modifiers) {
      if (!std::ranges::contains(modifier.allowed_values, spelling))
        continue;
      if (result && *result != modifier.kind_id) {
        throw ResolveException(fmt::format(
            "Descriptor for '{}' maps modifier spelling '{}' to both '{}' "
            "and '{}'.",
            instruction.Opcode_name, spelling, *result, modifier.kind_id));
      }
      result = modifier.kind_id;
    }
  }
  return result;
}

std::expected<ScalarType, ResolveDiagnostic> type_for_operand(
    const OperandSlotDescriptor& slot, const ResolvedInstructionFields& fields,
    const SourceRange& range) {
  if (slot.type_expr.empty())
    return ScalarType::Invalid;
  if (!slot.type_expr.starts_with('$')) {
    throw ResolveException(
        fmt::format("Operand slot '{}' has unsupported type expression '{}'.",
                    slot.field_id, slot.type_expr));
  }
  const std::string_view field_id = slot.type_expr.substr(1);
  const auto it = fields.modifiers.find(std::string(field_id));
  if (it == fields.modifiers.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Operand '{}' requires modifier '{}'.",
                               slot.field_id, field_id),
    });
  }
  if (const auto* type = std::get_if<WithLocs<ScalarType>>(&it->second))
    return type->value;
  throw ResolveException(fmt::format(
      "Operand '{}' expects modifier '{}' to resolve as ScalarType.",
      slot.field_id, field_id));
}

std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_operand_value(
    const OperandSlotDescriptor& slot, const syntax_ast::AstOperand& operand,
    const ResolvedInstructionFields& fields) {
  switch (slot.value_kind) {
    case ResolvedValueKind::Register: {
      auto value = resolve_register(operand);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Immediate: {
      const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand);
      if (immediate == nullptr) {
        return std::unexpected(ResolveDiagnostic{
            .range = std::visit(
                [](const auto& item) { return item.syntax.range; }, operand),
            .message = "Expected an immediate operand.",
        });
      }
      const auto type = type_for_operand(slot, fields, immediate->syntax.range);
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_immediate_value(*immediate, *type);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{WithLocs<ResolvedImmediate>{
          std::move(*value), immediate->syntax.range}};
    }
    case ResolvedValueKind::RegOrImm: {
      const auto type = type_for_operand(
          slot, fields,
          std::visit([](const auto& item) { return item.syntax.range; },
                     operand));
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_reg_or_imm(operand, *type);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Bool:
    case ResolvedValueKind::ScalarType:
      throw ResolveException(fmt::format(
          "Operand slot '{}' has a non-operand resolved value kind.",
          slot.field_id));
  }
  throw ResolveException("Unknown ResolvedValueKind.");
}

}  // namespace

std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::InstructionDescriptor& instruction) {
  ActualModifierTable result;
  for (const auto& modifier : ast.modifiers) {
    const auto kind_id = modifier_kind_id(instruction, modifier.syntax.text);
    if (!kind_id) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message =
              fmt::format("Unknown modifier '{}'.", modifier.syntax.text),
      });
    }

    const auto [_, inserted] = result.emplace(std::string(*kind_id), &modifier);
    if (!inserted) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message = fmt::format("Duplicate '{}' modifier.", *kind_id),
      });
    }
  }
  return result;
}

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::InstructionDescriptor& instruction,
    std::string_view variant_name) {
  const VariantDescriptor& variant =
      find_variant_descriptor(instruction, variant_name);
  const auto layout = select_operand_layout(variant, ast);
  if (!layout)
    return std::unexpected(layout.error());

  const auto actual_modifiers = collect_actual_modifiers(ast, instruction);
  if (!actual_modifiers)
    return std::unexpected(actual_modifiers.error());

  ResolvedInstructionFields fields{.variant_name = variant_name};
  for (const check_end::ModifierDescriptor& modifier : variant.modifiers) {
    const auto actual = actual_modifiers->find(std::string(modifier.kind_id));
    const bool present = actual != actual_modifiers->end();

    switch (modifier.value_kind) {
      case ResolvedValueKind::Bool:
        if (present) {
          fields.modifiers.emplace(
              modifier.kind_id,
              WithLocs<bool>{true, actual->second->syntax.range});
        } else if (modifier.presence ==
                   check_end::PresenceRequirement::Optional) {
          fields.modifiers.emplace(modifier.kind_id, WithLocs<bool>{false});
        }
        break;
      case ResolvedValueKind::ScalarType:
        if (!present) {
          if (modifier.presence == check_end::PresenceRequirement::Optional)
            break;
          return std::unexpected(ResolveDiagnostic{
              .range = ast.range,
              .message = fmt::format("Resolved variant requires '{}' modifier.",
                                     modifier.kind_id),
          });
        }
        {
          auto value = resolve_scalar_type(*actual->second);
          if (!value)
            return std::unexpected(value.error());
          fields.modifiers.emplace(modifier.kind_id, std::move(*value));
        }
        break;
      case ResolvedValueKind::Register:
      case ResolvedValueKind::Immediate:
      case ResolvedValueKind::RegOrImm:
        throw ResolveException(
            fmt::format("Modifier '{}' has a non-modifier resolved value kind.",
                        modifier.kind_id));
    }
  }

  for (size_t index = 0; index < ast.operands.size(); ++index) {
    const OperandSlotDescriptor& slot = (*layout)->slots[index];
    auto value = resolve_operand_value(slot, ast.operands[index], fields);
    if (!value)
      return std::unexpected(value.error());
    const auto [_, inserted] =
        fields.operands.emplace(std::string(slot.field_id), std::move(*value));
    if (!inserted) {
      throw ResolveException(
          fmt::format("Operand layout for variant '{}' repeats field '{}'.",
                      variant.variant_name, slot.field_id));
    }
  }

  return fields;
}

check_end::OperandSyntaxShape check_end::get_operand_syntax_shape(
    const syntax_ast::AstOperand& operand) {
  return std::visit(
      [](const auto& item) -> check_end::OperandSyntaxShape {
        using Item = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<Item, syntax_ast::AstIdentifierRef>) {
          return check_end::OperandSyntaxShape::Identifier;
        } else if constexpr (std::same_as<Item, syntax_ast::AstImmediate>) {
          return check_end::OperandSyntaxShape::Immediate;

        } else if constexpr (std::same_as<Item, syntax_ast::AstAddress>) {
          return check_end::OperandSyntaxShape::Address;

        } else if constexpr (std::same_as<Item, syntax_ast::AstVectorPack>) {
          return check_end::OperandSyntaxShape::VectorPack;

        } else if constexpr (std::same_as<Item, syntax_ast::AstVectorMember>) {
          return check_end::OperandSyntaxShape::VectorMember;
        }
      },
      operand);
}

bool matches_modifier_slot(const check_end::ModifierDescriptor& descriptor,
                           const ActualModifierTable& actual_modifiers) {
  const auto it = actual_modifiers.find(std::string(descriptor.kind_id));
  const bool present = it != actual_modifiers.end();

  switch (descriptor.presence) {
    case check_end::PresenceRequirement::Absent:
      return !present;

    case check_end::PresenceRequirement::Optional:
      if (!present)
        return true;
      return std::ranges::contains(descriptor.allowed_values,
                                   it->second->syntax.text);
    case check_end::PresenceRequirement::Required:
      if (!present)
        return false;
      return std::ranges::contains(descriptor.allowed_values,
                                   it->second->syntax.text);
  }

  throw ResolveException("Unknown PresenceRequirement.");
}

bool matches_variant(const check_end::VariantDescriptor& variant,
                     const ActualModifierTable& actual_modifiers) {
  std::unordered_set<std::string> declared_kinds;

  for (const auto& descriptor : variant.modifiers) {
    const auto [_, inserted] =
        declared_kinds.insert(std::string(descriptor.kind_id));
    if (!inserted) {
      throw ResolveException(
          fmt::format("Variant '{}' contains duplicate modifier kind '{}'.",
                      variant.variant_name, descriptor.kind_id));
    }

    if (!matches_modifier_slot(descriptor, actual_modifiers))
      return false;
  }

  // In the current model where "modifier combinations precisely determine variant",
  // each kind in actual must be explicitly described by this variant.
  for (const auto& [actual_kind, _] : actual_modifiers) {
    if (!declared_kinds.contains(actual_kind))
      return false;
  }

  return true;
}

};  // namespace ptx_frontend::resolved_ir
