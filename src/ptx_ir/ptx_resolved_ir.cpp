#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>
#include "resolved_value_domains.gen.hpp"

namespace ptx_frontend::resolved_ir::check_end {

bool SyntaxModifierDescriptor::check(std::string modifier_str) const {
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

int32_t SyntaxVariantDescriptor::get_required_modifier_num() const {
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

using check_end::OperandPresence;
using check_end::OperandSyntaxShape;
using check_end::ResolvedFieldDescriptor;
using check_end::ResolvedInstructionDescriptor;
using check_end::ResolvedModifierBindingDescriptor;
using check_end::ResolvedModifierDefaultKind;
using check_end::ResolvedOperandBindingDescriptor;
using check_end::ResolvedOperandLayoutDescriptor;
using check_end::ResolvedValueKind;
using check_end::ResolvedVariantDescriptor;
using check_end::SyntaxInstructionDescriptor;
using check_end::SyntaxModifierDescriptor;
using check_end::SyntaxOperandLayoutDescriptor;
using check_end::SyntaxOperandSlotDescriptor;
using check_end::SyntaxVariantDescriptor;

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
bool matches_operand_layout(const SyntaxOperandLayoutDescriptor& layout,
                            const syntax_ast::AstInstruction& ast) {
  if (ast.operands.size() > layout.slots.size())
    return false;

  for (size_t index = 0; index < layout.slots.size(); ++index) {
    const SyntaxOperandSlotDescriptor& slot = layout.slots[index];
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
 * Return whether ``candidate`` accepts a strict subset of ``other`` syntax.
 *
 * Availability is deliberately not considered here: resolve has no target
 * context. This makes an ``imm`` compatibility layout win over a later
 * ``reg_or_imm`` layout for the same immediate syntax, while still rejecting
 * layouts that are equally specific or incomparable.
 */
bool is_more_specific_operand_layout(
    const SyntaxOperandLayoutDescriptor& candidate,
    const SyntaxOperandLayoutDescriptor& other) {
  if (candidate.slots.size() != other.slots.size())
    return false;

  using Underlying = std::underlying_type_t<OperandSyntaxShape>;
  bool strictly_more_specific = false;
  for (size_t index = 0; index < candidate.slots.size(); ++index) {
    const auto& candidate_slot = candidate.slots[index];
    const auto& other_slot = other.slots[index];
    if (candidate_slot.presence != other_slot.presence)
      return false;

    const auto candidate_shapes =
        static_cast<Underlying>(candidate_slot.allowed_shapes);
    const auto other_shapes =
        static_cast<Underlying>(other_slot.allowed_shapes);
    if ((candidate_shapes & other_shapes) != candidate_shapes)
      return false;
    strictly_more_specific |= candidate_shapes != other_shapes;
  }
  return strictly_more_specific;
}

/**
 * @brief Select the operand layout descriptor that matches the actual operands of an instruction.
 * 
 * @param variant instruction variant descriptor
 * @param ast syntax AST instruction
 * @return the selected syntax layout and its index
 */
struct SelectedOperandLayout {
  const SyntaxOperandLayoutDescriptor& descriptor;
  size_t index;
};

std::expected<SelectedOperandLayout, ResolveDiagnostic> select_operand_layout(
    const SyntaxVariantDescriptor& variant,
    const syntax_ast::AstInstruction& ast) {
  std::vector<SelectedOperandLayout> matches;
  for (size_t index = 0; index < variant.operand_layouts.size(); ++index) {
    const auto& layout = variant.operand_layouts[index];
    if (!matches_operand_layout(layout, ast))
      continue;
    matches.push_back(
        SelectedOperandLayout{.descriptor = layout, .index = index});
  }

  if (matches.size() == 1)
    return matches.front();
  if (matches.size() > 1) {
    for (const auto& candidate : matches) {
      const bool more_specific_than_all = std::ranges::all_of(
          matches, [&candidate](const SelectedOperandLayout& other) {
            return candidate.index == other.index ||
                   is_more_specific_operand_layout(candidate.descriptor,
                                                   other.descriptor);
          });
      if (more_specific_than_all)
        return candidate;
    }
    throw ResolveException(fmt::format(
        "Descriptor variant '{}': multiple operand layouts match one syntax "
        "instruction without a unique most-specific layout.",
        variant.variant_name));
  }

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
 * @return const SyntaxVariantDescriptor&
 */
const SyntaxVariantDescriptor& find_syntax_variant_descriptor(
    const SyntaxInstructionDescriptor& instruction, std::string_view name) {
  const auto& descriptors = instruction.variants;
  const auto it = std::ranges::find_if(
      descriptors, [name](const SyntaxVariantDescriptor& descriptor) {
        return descriptor.variant_name == name;
      });
  if (it == descriptors.end()) {
    throw ResolveException(
        fmt::format("Descriptor for '{}' has no variant named '{}'.",
                    instruction.Opcode_name, name));
  }
  return *it;
}

template <typename T, size_t N>
constexpr std::optional<T> lookup_ptx_suffix(
    const std::array<generated_detail::PtxSuffixEntry<T>, N>& entries,
    std::string_view spelling) {
  if (spelling.starts_with('.'))
    spelling.remove_prefix(1);
  for (const auto& entry : entries) {
    if (entry.suffix == spelling)
      return entry.value;
  }
  return std::nullopt;
}

std::optional<ScalarType> scalar_type_from_ptx_name(std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kScalarTypes, spelling);
}

std::expected<WithLocs<ScalarType>, ResolveDiagnostic> resolve_scalar_type(
    const syntax_ast::AstModifier& modifier) {
  const auto type = scalar_type_from_ptx_name(modifier.syntax.text);
  if (!type) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown scalar type '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<ScalarType>{*type, modifier.syntax.range};
}

std::optional<RoundingMode> rounding_mode_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kRoundingModes, spelling);
}

std::expected<WithLocs<RoundingMode>, ResolveDiagnostic> resolve_rounding_mode(
    const syntax_ast::AstModifier& modifier) {
  const auto mode = rounding_mode_from_ptx_name(modifier.syntax.text);
  if (!mode) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown rounding mode '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<RoundingMode>{*mode, modifier.syntax.range};
}

struct ParsedNumberedRegister {
  std::string_view prefix;
  uint32_t index;
};

std::expected<ParsedNumberedRegister, ResolveDiagnostic>
parse_numbered_register(const syntax_ast::AstIdentifierRef& identifier,
                        SourceRange diagnostic_range,
                        std::string_view expected_description) {
  const std::string_view spelling = identifier.syntax.text;
  size_t digit_begin = spelling.size();
  while (digit_begin > 0 &&
         std::isdigit(static_cast<unsigned char>(spelling[digit_begin - 1]))) {
    --digit_begin;
  }
  if (spelling.size() < 3 || spelling.front() != '%' ||
      digit_begin == spelling.size() || digit_begin == 1) {
    return std::unexpected(ResolveDiagnostic{
        .range = diagnostic_range,
        .message = fmt::format("Expected {}, got '{}'.", expected_description,
                               spelling),
    });
  }

  uint32_t index = 0;
  const char* first = spelling.data() + digit_begin;
  const char* last = spelling.data() + spelling.size();
  const auto [end, error] = std::from_chars(first, last, index);
  if (error != std::errc{} || end != last) {
    return std::unexpected(ResolveDiagnostic{
        .range = diagnostic_range,
        .message =
            fmt::format("Register '{}' has an invalid numeric ID.", spelling),
    });
  }
  return ParsedNumberedRegister{
      .prefix = spelling.substr(1, digit_begin - 1),
      .index = index,
  };
}

std::optional<uint32_t> numbered_register_index(std::string_view spelling) {
  size_t digit_begin = spelling.size();
  while (digit_begin > 0 &&
         std::isdigit(static_cast<unsigned char>(spelling[digit_begin - 1]))) {
    --digit_begin;
  }
  if (digit_begin == spelling.size())
    return std::nullopt;

  uint32_t index = 0;
  const auto [end, error] = std::from_chars(
      spelling.data() + digit_begin, spelling.data() + spelling.size(), index);
  if (error != std::errc{} || end != spelling.data() + spelling.size())
    return std::nullopt;
  return index;
}

std::expected<ResolvedRegisterRef, ResolveDiagnostic> resolve_bound_register(
    const syntax_ast::AstIdentifierRef& identifier,
    ResolvedRegisterClass register_class, const ResolveContext& context,
    SourceRange range) {
  if (binding::isSpecialRegister(identifier.syntax.text)) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Special register '{}' is not supported by this resolved operand.",
            identifier.syntax.text),
    });
  }
  const auto lookup =
      context.symbols.lookup(context.scope, identifier.syntax.text);
  if (!lookup) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message =
            fmt::format("Unresolved register '{}'.", identifier.syntax.text),
    });
  }

  const binding::Symbol& symbol = context.symbols.symbol(lookup->symbol);
  if (symbol.kind != binding::SymbolKind::Variable ||
      symbol.state_space != syntax_ast::AstStateSpace::Register) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Symbol '{}' is not a .reg variable.",
                               identifier.syntax.text),
    });
  }
  if (!symbol.type) {
    throw ResolveException(fmt::format(
        "Register symbol '{}' has no declaration type.", symbol.name));
  }
  const auto declared_type = scalar_type_from_ptx_name(*symbol.type);
  if (!declared_type) {
    return std::unexpected(ResolveDiagnostic{
        .range = symbol.declaration_range,
        .message =
            fmt::format("Register '{}' has unsupported declared type '{}'.",
                        symbol.name, *symbol.type),
    });
  }

  const bool is_predicate = *declared_type == ScalarType::Pred;
  if (register_class == ResolvedRegisterClass::Predicate && !is_predicate) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Expected a predicate register, but '{}' is declared '{}'.",
            identifier.syntax.text, *symbol.type),
    });
  }
  if (register_class == ResolvedRegisterClass::General && is_predicate) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Expected a non-predicate register, but '{}' is declared '.pred'.",
            identifier.syntax.text),
    });
  }

  return ResolvedRegisterRef{
      .spelling = identifier.syntax.text,
      .register_class = register_class,
      .index = numbered_register_index(identifier.syntax.text),
      .symbol_id = lookup->symbol,
      .parameterized_index = lookup->parameterized_index,
      .declared_type = *declared_type,
  };
}

std::expected<WithLocs<ResolvedRegisterRef>, ResolveDiagnostic>
resolve_register(const syntax_ast::AstOperand& operand,
                 const ResolveContext* context) {
  const auto* identifier = std::get_if<syntax_ast::AstIdentifierRef>(&operand);
  if (identifier == nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a register operand.",
    });
  }

  if (context != nullptr) {
    auto value =
        resolve_bound_register(*identifier, ResolvedRegisterClass::General,
                               *context, identifier->syntax.range);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<ResolvedRegisterRef>{std::move(*value),
                                         identifier->syntax.range};
  }

  const auto parsed = parse_numbered_register(
      *identifier, identifier->syntax.range, "a numbered register");
  if (!parsed)
    return std::unexpected(parsed.error());
  if (parsed->prefix == "p") {
    return std::unexpected(ResolveDiagnostic{
        .range = identifier->syntax.range,
        .message = fmt::format("Expected a non-predicate register, got '{}'.",
                               identifier->syntax.text),
    });
  }
  return WithLocs<ResolvedRegisterRef>{
      ResolvedRegisterRef{
          .spelling = identifier->syntax.text,
          .register_class = ResolvedRegisterClass::General,
          .index = parsed->index,
      },
      identifier->syntax.range};
}

std::expected<WithLocs<ResolvedPredicate>, ResolveDiagnostic> resolve_predicate(
    const syntax_ast::AstOperand& operand, const ResolveContext* context) {
  const syntax_ast::AstIdentifierRef* identifier = nullptr;
  bool negated = false;
  SourceRange range;
  if (const auto* plain = std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    identifier = plain;
    range = plain->syntax.range;
  } else if (const auto* predicate =
                 std::get_if<syntax_ast::AstPredicateOperand>(&operand)) {
    identifier = &predicate->name;
    negated = predicate->negated;
    range = predicate->range;
  } else {
    return std::unexpected(ResolveDiagnostic{
        .range = syntax_ast::sourceRange(operand),
        .message = "Expected a predicate operand.",
    });
  }

  ResolvedRegisterRef register_ref{
      .register_class = ResolvedRegisterClass::Predicate,
  };
  if (context != nullptr) {
    auto value = resolve_bound_register(
        *identifier, ResolvedRegisterClass::Predicate, *context, range);
    if (!value)
      return std::unexpected(value.error());
    register_ref = std::move(*value);
  } else {
    const auto parsed = parse_numbered_register(
        *identifier, range, "a numbered predicate register");
    if (!parsed)
      return std::unexpected(parsed.error());
    if (parsed->prefix != "p") {
      return std::unexpected(ResolveDiagnostic{
          .range = range,
          .message = fmt::format("Expected a predicate register, got '{}'.",
                                 identifier->syntax.text),
      });
    }
    register_ref = ResolvedRegisterRef{
        .spelling = identifier->syntax.text,
        .register_class = ResolvedRegisterClass::Predicate,
        .index = parsed->index,
    };
  }

  return WithLocs<ResolvedPredicate>{
      ResolvedPredicate{.register_ref = std::move(register_ref),
                        .negated = negated},
      range};
}

ResolveDiagnostic invalid_immediate(const syntax_ast::AstImmediate& immediate,
                                    std::string message) {
  return ResolveDiagnostic{
      .range = immediate.syntax.range,
      .message = std::move(message),
  };
}

std::expected<uint64_t, ResolveDiagnostic> parse_unsigned_literal(
    const syntax_ast::AstImmediate& immediate, std::string_view text,
    int base) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);

  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size()) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Invalid integer literal '{}'.", immediate.syntax.text)));
  }
  return value;
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_integer_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    std::string_view text, bool negative, int base) {
  const ScalarKind kind = scalar_kind(type);
  if (kind != ScalarKind::Unsigned && kind != ScalarKind::Signed &&
      kind != ScalarKind::Bit) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format(
            "Integer literal '{}' is incompatible with scalar type '{}'.",
            immediate.syntax.text, to_string(type))));
  }

  const uint8_t byte_size = scalar_size_of(type);
  if (byte_size == 0 || byte_size > sizeof(uint64_t)) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Immediate type '{}' is not representable in 64 bits.",
                    to_string(type))));
  }
  const uint8_t bit_width = byte_size * 8;
  const uint64_t bit_mask = bit_width == 64
                                ? std::numeric_limits<uint64_t>::max()
                                : (uint64_t{1} << bit_width) - 1;

  if (base == 16 && (text.starts_with("0x") || text.starts_with("0X")))
    text.remove_prefix(2);
  const auto magnitude = parse_unsigned_literal(immediate, text, base);
  if (!magnitude)
    return std::unexpected(magnitude.error());

  uint64_t limit = bit_mask;
  if (kind == ScalarKind::Signed) {
    limit = negative ? (uint64_t{1} << (bit_width - 1))
                     : (uint64_t{1} << (bit_width - 1)) - 1;
  }
  if (*magnitude > limit) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format(
            "Integer literal '{}' is out of range for scalar type '{}'.",
            immediate.syntax.text, to_string(type))));
  }

  const uint64_t bits =
      negative ? (uint64_t{0} - *magnitude) & bit_mask : *magnitude;
  return ResolvedImmediate{.bits = bits, .type = type};
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_float_bits_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    std::string_view text, bool negative, uint8_t bit_width) {
  if (negative) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Floating bit-pattern literal '{}' cannot have a sign.",
                    immediate.syntax.text)));
  }
  const ScalarType expected_type =
      bit_width == 32 ? ScalarType::F32 : ScalarType::F64;
  if (type != expected_type) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format(
            "Floating bit-pattern literal '{}' requires scalar type '{}'.",
            immediate.syntax.text, to_string(expected_type))));
  }

  text.remove_prefix(2);  // 0f or 0d
  const auto bits = parse_unsigned_literal(immediate, text, 16);
  if (!bits)
    return std::unexpected(bits.error());
  return ResolvedImmediate{.bits = *bits, .type = type};
}

std::expected<ResolvedImmediate, ResolveDiagnostic>
resolve_decimal_float_literal(const syntax_ast::AstImmediate& immediate,
                              ScalarType type, std::string_view text) {
  if (type != ScalarType::F32 && type != ScalarType::F64) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Decimal floating literal '{}' is incompatible with scalar "
                    "type '{}'.",
                    immediate.syntax.text, to_string(type))));
  }

  double value = 0.0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value,
                      std::chars_format::general);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size() || !std::isfinite(value)) {
    return std::unexpected(invalid_immediate(
        immediate, fmt::format("Invalid decimal floating literal '{}'.",
                               immediate.syntax.text)));
  }

  if (type == ScalarType::F32) {
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) {
      return std::unexpected(invalid_immediate(
          immediate,
          fmt::format("Decimal floating literal '{}' is out of range for "
                      "scalar type '{}'.",
                      immediate.syntax.text, to_string(type))));
    }
    return ResolvedImmediate{.bits = std::bit_cast<uint32_t>(narrowed),
                             .type = type};
  }
  return ResolvedImmediate{.bits = std::bit_cast<uint64_t>(value),
                           .type = type};
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstImmediate& immediate, ScalarType type) {
  std::string_view text = immediate.syntax.text;
  bool negative = false;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }

  switch (immediate.kind) {
    case syntax_ast::AstImmediateKind::DecimalInteger:
      return resolve_integer_literal(immediate, type, text, negative, 10);
    case syntax_ast::AstImmediateKind::HexInteger:
      return resolve_integer_literal(immediate, type, text, negative, 16);
    case syntax_ast::AstImmediateKind::F32Hex:
      return resolve_float_bits_literal(immediate, type, text, negative, 32);
    case syntax_ast::AstImmediateKind::F64Hex:
      return resolve_float_bits_literal(immediate, type, text, negative, 64);
    case syntax_ast::AstImmediateKind::DecimalFloat:
      return resolve_decimal_float_literal(immediate, type,
                                           immediate.syntax.text);
  }
  throw ResolveException("Unknown AstImmediateKind.");
}

std::expected<WithLocs<RegOrImm>, ResolveDiagnostic> resolve_reg_or_imm(
    const syntax_ast::AstOperand& operand, ScalarType type,
    const ResolveContext* context) {
  if (const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand)) {
    auto register_ref = resolve_register(operand, context);
    if (!register_ref)
      return std::unexpected(register_ref.error());
    return WithLocs<RegOrImm>{RegOrImm{register_ref->value},
                              identifier->syntax.range};
  }
  if (const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand)) {
    auto value = resolve_immediate_value(*immediate, type);
    if (!value)
      return std::unexpected(value.error());
    return WithLocs<RegOrImm>{RegOrImm{*value}, immediate->syntax.range};
  }
  return std::unexpected(ResolveDiagnostic{
      .range = syntax_ast::sourceRange(operand),
      .message = "Expected a register or immediate operand.",
  });
}

struct ModifierBindingAttempt {
  std::optional<ActualModifierTable> modifiers;
  const syntax_ast::AstModifier* duplicate = nullptr;
  std::string_view duplicate_slot;
};

/**
 * Bind source modifier spellings to the slots of one candidate variant.
 *
 * Slot IDs are variant-local. The same spelling may therefore denote `type`
 * in one variant and `result_type` in another. Within one variant, however,
 * every spelling must have exactly one active owner; the Python database
 * validator enforces this invariant and a violation here is a compiler bug.
 */
ModifierBindingAttempt bind_variant_modifiers(
    const syntax_ast::AstInstruction& ast,
    const SyntaxVariantDescriptor& variant) {
  std::unordered_set<std::string_view> slot_ids;
  for (const auto& descriptor : variant.modifiers) {
    if (!slot_ids.insert(descriptor.kind_id).second) {
      throw ResolveException(
          fmt::format("Variant '{}' contains duplicate modifier slot '{}'.",
                      variant.variant_name, descriptor.kind_id));
    }
  }

  ActualModifierTable result;
  for (const auto& actual : ast.modifiers) {
    const SyntaxModifierDescriptor* owner = nullptr;
    for (const auto& descriptor : variant.modifiers) {
      if (descriptor.presence == check_end::PresenceRequirement::Absent ||
          !std::ranges::contains(descriptor.allowed_values,
                                 actual.syntax.text)) {
        continue;
      }
      if (owner != nullptr) {
        throw ResolveException(fmt::format(
            "Variant '{}' maps modifier spelling '{}' to both '{}' and '{}'.",
            variant.variant_name, actual.syntax.text, owner->kind_id,
            descriptor.kind_id));
      }
      owner = &descriptor;
    }

    if (owner == nullptr)
      return {};

    const auto [_, inserted] =
        result.emplace(std::string(owner->kind_id), &actual);
    if (!inserted) {
      return ModifierBindingAttempt{
          .duplicate = &actual,
          .duplicate_slot = owner->kind_id,
      };
    }
  }

  for (const auto& descriptor : variant.modifiers) {
    if (descriptor.presence == check_end::PresenceRequirement::Required &&
        !result.contains(std::string(descriptor.kind_id))) {
      return {};
    }
  }

  return ModifierBindingAttempt{.modifiers = std::move(result)};
}

bool is_known_modifier_spelling(const SyntaxInstructionDescriptor& instruction,
                                std::string_view spelling) {
  return std::ranges::any_of(
      instruction.variants, [spelling](const auto& variant) {
        return std::ranges::any_of(
            variant.modifiers, [spelling](const auto& modifier) {
              return std::ranges::contains(modifier.allowed_values, spelling);
            });
      });
}

const ResolvedVariantDescriptor& find_resolved_variant_descriptor(
    const ResolvedInstructionDescriptor& instruction, std::string_view name) {
  const auto it =
      std::ranges::find_if(instruction.variants,
                           [name](const ResolvedVariantDescriptor& descriptor) {
                             return descriptor.variant_name == name;
                           });
  if (it == instruction.variants.end()) {
    throw ResolveException(
        fmt::format("Resolved descriptor for '{}' has no variant named '{}'.",
                    instruction.opcode_name, name));
  }
  return *it;
}

const ResolvedFieldDescriptor& find_resolved_field_descriptor(
    const ResolvedVariantDescriptor& variant, std::string_view field_id) {
  const auto it = std::ranges::find_if(
      variant.fields, [field_id](const ResolvedFieldDescriptor& descriptor) {
        return descriptor.field_id == field_id;
      });
  if (it == variant.fields.end()) {
    throw ResolveException(
        fmt::format("Resolved descriptor variant '{}' has no field named '{}'.",
                    variant.variant_name, field_id));
  }
  return *it;
}

const ResolvedFieldDescriptor& find_resolved_operand_field_descriptor(
    const ResolvedOperandLayoutDescriptor& layout, std::string_view field_id) {
  const auto it = std::ranges::find_if(
      layout.fields, [field_id](const ResolvedFieldDescriptor& descriptor) {
        return descriptor.field_id == field_id;
      });
  if (it == layout.fields.end()) {
    throw ResolveException(
        fmt::format("Resolved operand layout '{}' has no field named '{}'.",
                    layout.layout_id, field_id));
  }
  return *it;
}

const SyntaxModifierDescriptor& find_syntax_modifier_descriptor(
    const SyntaxVariantDescriptor& variant, std::string_view kind_id) {
  const auto it = std::ranges::find_if(
      variant.modifiers, [kind_id](const SyntaxModifierDescriptor& descriptor) {
        return descriptor.kind_id == kind_id;
      });
  if (it == variant.modifiers.end()) {
    throw ResolveException(
        fmt::format("Syntax descriptor variant '{}' has no modifier kind '{}'.",
                    variant.variant_name, kind_id));
  }
  return *it;
}

std::expected<ScalarType, ResolveDiagnostic> type_for_operand(
    const ResolvedOperandBindingDescriptor& binding,
    const ResolvedInstructionFields& fields, const SourceRange& range) {
  const auto& expression = binding.type_expression;
  if (expression.kind == checker::OperandTypeExpressionKind::None)
    return ScalarType::Invalid;
  if (expression.kind == checker::OperandTypeExpressionKind::FixedScalar)
    return expression.fixed_scalar_type;
  if (expression.kind != checker::OperandTypeExpressionKind::ModifierField ||
      expression.modifier_field_id.empty()) {
    throw ResolveException(
        fmt::format("Resolved operand field '{}' has an invalid type "
                    "expression descriptor.",
                    binding.target_field_id));
  }

  const std::string_view field_id = expression.modifier_field_id;
  const auto it = fields.modifiers.find(std::string(field_id));
  if (it == fields.modifiers.end()) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format("Operand '{}' requires modifier '{}'.",
                               binding.target_field_id, field_id),
    });
  }
  if (const auto* type = std::get_if<WithLocs<ScalarType>>(&it->second))
    return type->value;
  throw ResolveException(fmt::format(
      "Operand '{}' expects modifier '{}' to resolve as ScalarType.",
      binding.target_field_id, field_id));
}

std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_operand_value(
    const ResolvedFieldDescriptor& field,
    const ResolvedOperandBindingDescriptor& binding,
    const syntax_ast::AstOperand& operand,
    const ResolvedInstructionFields& fields, const ResolveContext* context) {
  switch (field.value_kind) {
    case ResolvedValueKind::Register: {
      auto value = resolve_register(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Predicate: {
      auto value = resolve_predicate(operand, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Immediate: {
      const auto* immediate = std::get_if<syntax_ast::AstImmediate>(&operand);
      if (immediate == nullptr) {
        return std::unexpected(ResolveDiagnostic{
            .range = syntax_ast::sourceRange(operand),
            .message = "Expected an immediate operand.",
        });
      }
      const auto type =
          type_for_operand(binding, fields, immediate->syntax.range);
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_immediate_value(*immediate, *type);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{WithLocs<ResolvedImmediate>{
          std::move(*value), immediate->syntax.range}};
    }
    case ResolvedValueKind::RegOrImm: {
      const auto type =
          type_for_operand(binding, fields, syntax_ast::sourceRange(operand));
      if (!type)
        return std::unexpected(type.error());
      auto value = resolve_reg_or_imm(operand, *type, context);
      if (!value)
        return std::unexpected(value.error());
      return ResolvedFieldValue{std::move(*value)};
    }
    case ResolvedValueKind::Bool:
    case ResolvedValueKind::ScalarType:
    case ResolvedValueKind::RoundingMode:
      throw ResolveException(fmt::format(
          "Operand slot '{}' has a non-operand resolved value kind.",
          field.field_id));
  }
  throw ResolveException("Unknown ResolvedValueKind.");
}

ResolvedFieldValue resolve_default_modifier_value(
    const ResolvedFieldDescriptor& field,
    const ResolvedModifierBindingDescriptor& binding) {
  const auto& default_value = binding.default_value;
  switch (field.value_kind) {
    case ResolvedValueKind::Bool:
      if (default_value.kind != ResolvedModifierDefaultKind::Bool) {
        throw ResolveException(fmt::format(
            "Optional modifier '{}' requires a boolean default for resolved "
            "field '{}'.",
            binding.source_kind_id, field.field_id));
      }
      return ResolvedFieldValue{WithLocs<bool>{default_value.bool_value}};
    case ResolvedValueKind::ScalarType:
      if (default_value.kind != ResolvedModifierDefaultKind::ScalarType ||
          default_value.scalar_type == ScalarType::Invalid) {
        throw ResolveException(fmt::format(
            "Optional modifier '{}' requires a scalar-type default for "
            "resolved field '{}'.",
            binding.source_kind_id, field.field_id));
      }
      return ResolvedFieldValue{
          WithLocs<ScalarType>{default_value.scalar_type}};
    case ResolvedValueKind::RoundingMode:
      if (default_value.kind != ResolvedModifierDefaultKind::RoundingMode ||
          default_value.rounding_mode == RoundingMode::Invalid) {
        throw ResolveException(fmt::format(
            "Optional modifier '{}' requires a rounding-mode default for "
            "resolved field '{}'.",
            binding.source_kind_id, field.field_id));
      }
      return ResolvedFieldValue{
          WithLocs<RoundingMode>{default_value.rounding_mode}};
    case ResolvedValueKind::Register:
    case ResolvedValueKind::Predicate:
    case ResolvedValueKind::Immediate:
    case ResolvedValueKind::RegOrImm:
      throw ResolveException(fmt::format(
          "Optional modifier '{}' targets non-modifier resolved field '{}'.",
          binding.source_kind_id, field.field_id));
  }
  throw ResolveException("Unknown ResolvedValueKind.");
}

}  // namespace

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type) {
  return resolve_immediate_value(immediate, type);
}

std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxVariantDescriptor& variant) {
  auto attempt = bind_variant_modifiers(ast, variant);
  if (attempt.modifiers)
    return std::move(*attempt.modifiers);
  if (attempt.duplicate != nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = attempt.duplicate->syntax.range,
        .message =
            fmt::format("Duplicate '{}' modifier.", attempt.duplicate_slot),
    });
  }
  return std::unexpected(ResolveDiagnostic{
      .range = ast.range,
      .message = fmt::format(
          "Modifier combination does not match instruction variant '{}'.",
          variant.variant_name),
  });
}

std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction) {
  if (ast.opcode.syntax.text != instruction.Opcode_name) {
    return std::unexpected(ResolveDiagnostic{
        .range = ast.opcode.syntax.range,
        .message = fmt::format("Cannot resolve opcode '{}' as '{}'.",
                               ast.opcode.syntax.text, instruction.Opcode_name),
    });
  }

  for (const auto& modifier : ast.modifiers) {
    if (!is_known_modifier_spelling(instruction, modifier.syntax.text)) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message =
              fmt::format("Unknown modifier '{}'.", modifier.syntax.text),
      });
    }
  }

  std::optional<std::string_view> selected;
  const syntax_ast::AstModifier* duplicate = nullptr;
  std::string_view duplicate_slot;
  for (const auto& variant : instruction.variants) {
    auto attempt = bind_variant_modifiers(ast, variant);
    if (!attempt.modifiers) {
      if (duplicate == nullptr && attempt.duplicate != nullptr) {
        duplicate = attempt.duplicate;
        duplicate_slot = attempt.duplicate_slot;
      }
      continue;
    }

    if (selected) {
      return std::unexpected(ResolveDiagnostic{
          .range = ast.range,
          .message = fmt::format(
              "Ambiguous modifier combination for instruction '{}'.",
              ast.opcode.syntax.text),
      });
    }
    selected = variant.variant_name;
  }

  if (selected)
    return *selected;
  if (duplicate != nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = duplicate->syntax.range,
        .message = fmt::format("Duplicate '{}' modifier.", duplicate_slot),
    });
  }
  return std::unexpected(ResolveDiagnostic{
      .range = ast.range,
      .message = fmt::format(
          "No variant of instruction '{}' accepts this modifier combination.",
          ast.opcode.syntax.text),
  });
}

std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& syntax_instruction,
    const check_end::ResolvedInstructionDescriptor& resolved_instruction,
    std::string_view variant_name, const ResolveContext* context) {
  const SyntaxVariantDescriptor& syntax_variant =
      find_syntax_variant_descriptor(syntax_instruction, variant_name);
  const ResolvedVariantDescriptor& resolved_variant =
      find_resolved_variant_descriptor(resolved_instruction, variant_name);
  const auto selected_layout = select_operand_layout(syntax_variant, ast);
  if (!selected_layout)
    return std::unexpected(selected_layout.error());

  if (selected_layout->index >= resolved_variant.operand_layouts.size()) {
    throw ResolveException(fmt::format(
        "Resolved descriptor variant '{}' has no operand binding layout at "
        "syntax layout index {}.",
        variant_name, selected_layout->index));
  }
  if (selected_layout->index > std::numeric_limits<uint16_t>::max()) {
    throw ResolveException(fmt::format(
        "Operand layout index {} for variant '{}' exceeds the resolved IR "
        "layout-tag range.",
        selected_layout->index, variant_name));
  }
  const auto& resolved_layout =
      resolved_variant.operand_layouts[selected_layout->index];
  if (resolved_layout.bindings.size() !=
      selected_layout->descriptor.slots.size()) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}': syntax layout {} has {} slots but resolved "
        "binding layout has {} entries.",
        variant_name, selected_layout->index,
        selected_layout->descriptor.slots.size(),
        resolved_layout.bindings.size()));
  }

  const auto actual_modifiers = collect_actual_modifiers(ast, syntax_variant);
  if (!actual_modifiers)
    return std::unexpected(actual_modifiers.error());

  ResolvedInstructionFields fields{
      .variant_name = variant_name,
      .operand_layout = ResolvedOperandLayoutTag{static_cast<uint16_t>(
          selected_layout->index)},
  };
  for (const auto& binding : resolved_variant.modifier_bindings) {
    const auto& syntax_modifier =
        find_syntax_modifier_descriptor(syntax_variant, binding.source_kind_id);
    const auto& field = find_resolved_field_descriptor(resolved_variant,
                                                       binding.target_field_id);
    const auto actual =
        actual_modifiers->find(std::string(binding.source_kind_id));
    const bool present = actual != actual_modifiers->end();

    if (syntax_modifier.presence == check_end::PresenceRequirement::Absent) {
      throw ResolveException(fmt::format(
          "Resolved binding '{}' refers to absent syntax modifier '{}'.",
          binding.target_field_id, binding.source_kind_id));
    }

    if (!present) {
      if (syntax_modifier.presence ==
          check_end::PresenceRequirement::Optional) {
        fields.modifiers.emplace(
            field.field_id, resolve_default_modifier_value(field, binding));
        continue;
      }
      return std::unexpected(ResolveDiagnostic{
          .range = ast.range,
          .message = fmt::format("Resolved variant requires '{}' modifier.",
                                 binding.source_kind_id),
      });
    }

    switch (field.value_kind) {
      case ResolvedValueKind::Bool:
        fields.modifiers.emplace(
            field.field_id, WithLocs<bool>{true, actual->second->syntax.range});
        break;
      case ResolvedValueKind::ScalarType: {
        auto value = resolve_scalar_type(*actual->second);
        if (!value)
          return std::unexpected(value.error());
        fields.modifiers.emplace(field.field_id, std::move(*value));
      } break;
      case ResolvedValueKind::RoundingMode: {
        auto value = resolve_rounding_mode(*actual->second);
        if (!value)
          return std::unexpected(value.error());
        fields.modifiers.emplace(field.field_id, std::move(*value));
      } break;
      case ResolvedValueKind::Register:
      case ResolvedValueKind::Predicate:
      case ResolvedValueKind::Immediate:
      case ResolvedValueKind::RegOrImm:
        throw ResolveException(
            fmt::format("Modifier '{}' has a non-modifier resolved value kind.",
                        binding.source_kind_id));
    }
  }

  for (size_t index = 0; index < ast.operands.size(); ++index) {
    const auto& binding = resolved_layout.bindings[index];
    const auto& field = find_resolved_operand_field_descriptor(
        resolved_layout, binding.target_field_id);
    auto value = resolve_operand_value(field, binding, ast.operands[index],
                                       fields, context);
    if (!value)
      return std::unexpected(value.error());
    const auto [_, inserted] =
        fields.operands.emplace(std::string(field.field_id), std::move(*value));
    if (!inserted) {
      throw ResolveException(
          fmt::format("Operand layout for variant '{}' repeats field '{}'.",
                      syntax_variant.variant_name, field.field_id));
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

        } else if constexpr (std::same_as<Item,
                                          syntax_ast::AstPredicateOperand>) {
          return check_end::OperandSyntaxShape::Predicate;

        } else if constexpr (std::same_as<Item, syntax_ast::AstAddress>) {
          return check_end::OperandSyntaxShape::Address;

        } else if constexpr (std::same_as<Item, syntax_ast::AstVectorPack>) {
          return check_end::OperandSyntaxShape::VectorPack;

        } else if constexpr (std::same_as<Item, syntax_ast::AstVectorMember>) {
          return check_end::OperandSyntaxShape::VectorMember;
        } else if constexpr (std::same_as<Item,
                                          syntax_ast::AstCallParameterList>) {
          return check_end::OperandSyntaxShape::Group;
        } else if constexpr (std::same_as<Item, syntax_ast::AstCallTarget>) {
          return check_end::OperandSyntaxShape::CallTarget;
        } else if constexpr (std::same_as<Item, syntax_ast::AstCallTargetSet>) {
          return check_end::OperandSyntaxShape::CallTargetSet;
        } else {
          return check_end::OperandSyntaxShape::BranchTarget;
        }
      },
      operand);
}

};  // namespace ptx_frontend::resolved_ir
