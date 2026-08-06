#include "ptx_ir/ptx_resolved_ir.hpp"
#include <frozen/string.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ptx_frontend::resolved_ir::check_end {

bool ModifierDescriptor::check(std::string modifier_str) const {
  if (this->presence == PresenceRequirement::Absent) {
    return this->allowed_values.find(modifier_str) ==
           this->allowed_values.end();
  } else if (this->presence == PresenceRequirement::Optional) {
    return true;  // any value is allowed
  } else if (this->presence == PresenceRequirement::Required) {
    return this->allowed_values.find(modifier_str) !=
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

void test(const syntax_ast::AstInstruction& ast) {
  auto r = selectVariant<Add>(ast);
}

template <PtxOperator T>
std::expected<T, ResolveDiagnostic> resolve(
    const syntax_ast::AstInstruction& ast) {

  auto target_variant = selectVariant<T>(ast);
}

check_end::InstructionDescriptor Add::get_inst_descriptor() {
  // check_end::OperandDescriptor dst_desc{
  //     .name = "dst",
  //     .role = check_end::OperandRole::Destination,
  //     .access = check_end::OperandAccess::Write,
  //     .allowed_shapes = check_end::OperandShape::Register,
  //     .allowed_state_spaces = StateSpace::Reg,
  // };
  // check_end::OperandDescriptor src1_desc{
  //     .name = "src1",
  //     .role = check_end::OperandRole::Source,
  //     .access = check_end::OperandAccess::Read,
  //     .allowed_shapes = check_end::OperandShape::Register |
  //                       check_end::OperandShape::Immediate};
  // check_end::OperandDescriptor src2_desc{
  //     .name = "src2",
  //     .role = check_end::OperandRole::Source,
  //     .access = check_end::OperandAccess::Read,
  //     .allowed_shapes = check_end::OperandShape::Register |
  //                       check_end::OperandShape::Immediate};

  // check_end::VariantDescriptor integer_no_sat_desc{
  //     .variant_name = "AddIntegerNoSat",
  //     .modifiers = {{
  //                       .allowed_values = {".sat"},
  //                       .presence = check_end::PresenceRequirement::Absent,
  //                       .kind_id = "sat",
  //                   },
  //                   {
  //                       .allowed_values = {".u16", ".u32", ".u64", ".s16",
  //                                          ".s32", ".s64"},
  //                       .presence = check_end::PresenceRequirement::Absent,
  //                       .kind_id = "type",
  //                   }},
  //     .operands = {dst_desc, src1_desc, src2_desc}};

  // check_end::InstructionDescriptor inst_desc{
  //     .Opcode_name = "add",
  //     .variants = {integer_no_sat_desc},
  // };
  // return inst_desc;
}

bool matches_modifier_slot(const check_end::ModifierDescriptor& descriptor,
                           const ActualModifierTable& actual_modifiers) {
  const auto it = actual_modifiers.find(descriptor.kind_id);
  const bool present = it != actual_modifiers.end();

  switch (descriptor.presence) {
    case check_end::PresenceRequirement::Absent:
      return !present;

    case check_end::PresenceRequirement::Optional:
      if (!present)
        return true;
      return descriptor.allowed_values.contains(it->second->syntax.text);

    case check_end::PresenceRequirement::Required:
      if (!present)
        return false;
      return descriptor.allowed_values.contains(it->second->syntax.text);
  }

  throw ResolveException("Unknown PresenceRequirement.");
}

bool matches_variant(const check_end::VariantDescriptor& variant,
                     const ActualModifierTable& actual_modifiers) {
  std::unordered_set<std::string> declared_kinds;

  for (const auto& descriptor : variant.modifiers) {
    const auto [_, inserted] = declared_kinds.insert(descriptor.kind_id);
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
