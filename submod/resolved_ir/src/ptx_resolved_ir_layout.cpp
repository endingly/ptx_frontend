#include "ptx_resolved_ir_layout.hpp"

#include <algorithm>
#include <type_traits>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

namespace ptx_frontend::resolved_ir::check_end {

OperandSyntaxShape get_operand_syntax_shape(
    const syntax_ast::AstOperand& operand) {
  return std::visit(
      [](const auto& item) -> OperandSyntaxShape {
        using Item = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<Item, syntax_ast::AstIdentifierRef>)
          return OperandSyntaxShape::Identifier;
        else if constexpr (std::same_as<Item, syntax_ast::AstImmediate>)
          return OperandSyntaxShape::Immediate;
        else if constexpr (std::same_as<Item,
                                        syntax_ast::AstPredicateOperand>)
          return OperandSyntaxShape::Predicate;
        else if constexpr (std::same_as<Item, syntax_ast::AstAddress>)
          return OperandSyntaxShape::Address;
        else if constexpr (std::same_as<Item, syntax_ast::AstVectorPack>)
          return OperandSyntaxShape::VectorPack;
        else if constexpr (std::same_as<Item, syntax_ast::AstVectorMember>)
          return OperandSyntaxShape::VectorMember;
        else if constexpr (std::same_as<Item,
                                        syntax_ast::AstCallParameterList>)
          return OperandSyntaxShape::Group;
        else if constexpr (std::same_as<Item, syntax_ast::AstCallTarget>)
          return OperandSyntaxShape::CallTarget;
        else if constexpr (std::same_as<Item, syntax_ast::AstCallTargetSet>)
          return OperandSyntaxShape::CallTargetSet;
        else if constexpr (std::same_as<Item,
                                        syntax_ast::AstBranchTargetSet>)
          return OperandSyntaxShape::BranchTargetSet;
        else if constexpr (std::same_as<Item,
                                        syntax_ast::AstRegisterPredicatePair>)
          return OperandSyntaxShape::RegisterPredicatePair;
        else
          return OperandSyntaxShape::BranchTarget;
      },
      operand);
}

}  // namespace ptx_frontend::resolved_ir::check_end

namespace ptx_frontend::resolved_ir {
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

namespace detail {
static bool allows_shape(OperandSyntaxShape allowed, OperandSyntaxShape actual) {
  using Underlying = std::underlying_type_t<OperandSyntaxShape>;
  return (static_cast<Underlying>(allowed) & static_cast<Underlying>(actual)) !=
         0;
}

static bool allows_shape(checker::OperandShape allowed,
                         checker::OperandShape actual) {
  using Underlying = std::underlying_type_t<checker::OperandShape>;
  return (static_cast<Underlying>(allowed) & static_cast<Underlying>(actual)) !=
         0;
}

OperandSyntaxShape vector_element_syntax_shape(
    const syntax_ast::AstVectorElement& element) {
  return std::visit(
      [](const auto& value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_ast::AstIdentifierRef>)
          return OperandSyntaxShape::Identifier;
        else
          return OperandSyntaxShape::Immediate;
      },
      element);
}

bool matches_operand_slot(const SyntaxOperandSlotDescriptor& slot,
                          const syntax_ast::AstOperand& operand) {
  if (!allows_shape(slot.allowed_shapes,
                    check_end::get_operand_syntax_shape(operand))) {
    return false;
  }

  const auto* vector = std::get_if<syntax_ast::AstVectorPack>(&operand);
  if (vector == nullptr)
    return true;
  if (slot.minimum_elements != 0 &&
      (vector->elements.size() < slot.minimum_elements ||
       vector->elements.size() > slot.maximum_elements)) {
    return false;
  }
  if (slot.allowed_element_shapes == OperandSyntaxShape{})
    return true;
  return std::ranges::all_of(vector->elements, [&slot](const auto& element) {
    return allows_shape(slot.allowed_element_shapes,
                        vector_element_syntax_shape(element));
  });
}

std::optional<ResolveDiagnostic> diagnose_modern_pack_mismatch(
    const SyntaxOperandLayoutDescriptor& layout,
    const syntax_ast::AstInstruction& ast) {
  if (ast.operands.size() != layout.slots.size())
    return std::nullopt;

  for (size_t index = 0; index < layout.slots.size(); ++index) {
    const auto& slot = layout.slots[index];
    const auto* vector =
        std::get_if<syntax_ast::AstVectorPack>(&ast.operands[index]);
    if (slot.minimum_elements == 0 || vector == nullptr ||
        !allows_shape(slot.allowed_shapes,
                      check_end::get_operand_syntax_shape(ast.operands[index]))) {
      continue;
    }
    bool other_slots_match = true;
    for (size_t other_index = 0; other_index < layout.slots.size();
         ++other_index) {
      if (other_index != index &&
          !matches_operand_slot(layout.slots[other_index],
                                ast.operands[other_index])) {
        other_slots_match = false;
        break;
      }
    }
    if (!other_slots_match)
      continue;
    if (vector->elements.size() < slot.minimum_elements ||
        vector->elements.size() > slot.maximum_elements) {
      return ResolveDiagnostic{
          .range = vector->range,
          .message = fmt::format("Vector operand requires {} to {} elements.",
                                 slot.minimum_elements, slot.maximum_elements),
      };
    }
    for (const auto& element : vector->elements) {
      if (allows_shape(slot.allowed_element_shapes,
                       vector_element_syntax_shape(element))) {
        continue;
      }
      const auto range = std::visit(
          [](const auto& value) { return value.syntax.range; }, element);
      return ResolveDiagnostic{
          .range = range,
          .message =
              "Vector operand element has a shape not accepted by this "
              "instruction layout.",
      };
    }
  }
  return std::nullopt;
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
  if (layout.kind == check_end::OperandLayoutKind::Call ||
      layout.kind == check_end::OperandLayoutKind::IndirectCall) {
    if (ast.operands.size() != layout.slots.size())
      return false;
    for (size_t index = 0; index < layout.slots.size(); ++index) {
      if (!matches_operand_slot(layout.slots[index], ast.operands[index])) {
        return false;
      }
    }
    const bool indirect =
        layout.kind == check_end::OperandLayoutKind::IndirectCall;
    const size_t minimum_operands = indirect ? 2 : 1;
    if (ast.operands.size() == minimum_operands)
      return true;
    if (ast.operands.size() == minimum_operands + 1) {
      const auto* arguments =
          std::get_if<syntax_ast::AstCallParameterList>(&ast.operands[1]);
      return arguments != nullptr &&
             arguments->kind == syntax_ast::AstCallParameterListKind::Input;
    }
    if (ast.operands.size() == minimum_operands + 2) {
      const auto* returns =
          std::get_if<syntax_ast::AstCallParameterList>(&ast.operands[0]);
      const auto* arguments =
          std::get_if<syntax_ast::AstCallParameterList>(&ast.operands[2]);
      return returns != nullptr && arguments != nullptr &&
             returns->kind == syntax_ast::AstCallParameterListKind::Return &&
             arguments->kind == syntax_ast::AstCallParameterListKind::Input;
    }
    return false;
  }
  if (ast.operands.size() > layout.slots.size())
    return false;

  for (size_t index = 0; index < layout.slots.size(); ++index) {
    const SyntaxOperandSlotDescriptor& slot = layout.slots[index];
    if (index == ast.operands.size()) {
      if (slot.presence == OperandPresence::Required)
        return false;
      continue;
    }

    if (!matches_operand_slot(slot, ast.operands[index])) {
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
    if (candidate_slot.presence == OperandPresence::Optional &&
        other_slot.presence == OperandPresence::Required)
      return false;
    strictly_more_specific |= candidate_slot.presence != other_slot.presence;

    const auto candidate_shapes =
        static_cast<Underlying>(candidate_slot.allowed_shapes);
    const auto other_shapes =
        static_cast<Underlying>(other_slot.allowed_shapes);
    if ((candidate_shapes & other_shapes) != candidate_shapes)
      return false;
    strictly_more_specific |= candidate_shapes != other_shapes;

    if (candidate_slot.minimum_elements == 0) {
      if (other_slot.minimum_elements != 0)
        return false;
      continue;
    }

    if (other_slot.minimum_elements != 0) {
      if (candidate_slot.minimum_elements < other_slot.minimum_elements ||
          candidate_slot.maximum_elements > other_slot.maximum_elements) {
        return false;
      }
      strictly_more_specific |=
          candidate_slot.minimum_elements != other_slot.minimum_elements ||
          candidate_slot.maximum_elements != other_slot.maximum_elements;
    } else {
      strictly_more_specific = true;
    }

    if (other_slot.allowed_element_shapes == OperandSyntaxShape{})
      continue;
    if (candidate_slot.allowed_element_shapes == OperandSyntaxShape{})
      return false;

    const auto candidate_element_shapes =
        static_cast<Underlying>(candidate_slot.allowed_element_shapes);
    const auto other_element_shapes =
        static_cast<Underlying>(other_slot.allowed_element_shapes);
    if ((candidate_element_shapes & other_element_shapes) !=
        candidate_element_shapes) {
      return false;
    }
    strictly_more_specific |= candidate_element_shapes != other_element_shapes;
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

  for (const auto& layout : variant.operand_layouts) {
    if (const auto diagnostic = diagnose_modern_pack_mismatch(layout, ast))
      return std::unexpected(*diagnostic);
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

}  // namespace detail
}  // namespace ptx_frontend::resolved_ir
