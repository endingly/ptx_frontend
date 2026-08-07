#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include <algorithm>
#include <array>
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

check_end::OperandSyntaxShape get_operand_syntax_shape(
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
