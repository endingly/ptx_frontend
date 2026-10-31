#include "ptx_call_parameter_transfers.hpp"

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>

#include <algorithm>
#include <bit>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** The actual declaration/member, independent of address index or byte addend. */
struct ParameterObject {
  /** Module-owned declaration identity; no spelling rejoin is needed. */
  binding::SymbolId symbol;
  /** Compact declaration member, never an address subscript. */
  std::optional<uint32_t> member;
  /** Compare declaration/member identity without address offset or spelling. */
  bool operator==(const ParameterObject&) const = default;
};
/** A copied, normalized local-object transfer; symbolic indices remain unevaluated. */
struct ParameterTransfer {
  ParameterObject object;
  /** Store before an input call, or load after a returning call. */
  bool store{};
  /** Known byte contribution, without an assertion of execution bounds. */
  int64_t bytes{};
  /** Exact symbolic index carrier, separate from the compact member identity. */
  std::optional<ResolvedRegisterRef> index;
};
/** Borrow the address only from the canonical Parameter-capable LD/ST forms. */
const ResolvedAddress* parameter_address(const Instruction& instruction,
                                         bool& store) {
  if (const auto* value = dynamic_cast<const LdExplicitScalar*>(&instruction))
    return value->state_space.value == MemoryStateSpace::Parameter
               ? &value->address.value
               : nullptr;
  if (const auto* value = dynamic_cast<const LdExplicitVector*>(&instruction))
    return value->state_space.value == MemoryStateSpace::Parameter
               ? &value->address.value
               : nullptr;
  store = true;
  if (const auto* value = dynamic_cast<const StExplicitScalar*>(&instruction))
    return value->state_space.value == MemoryStateSpace::Parameter
               ? &value->address.value
               : nullptr;
  if (const auto* value = dynamic_cast<const StExplicitVector*>(&instruction))
    return value->state_space.value == MemoryStateSpace::Parameter
               ? &value->address.value
               : nullptr;
  return nullptr;
}
/** Copy call identities while reference callbacks borrow the selected payload. */
struct CallObjects final : IReferenceObserver {
  /** Owned actual identities, copied in source argument order. */
  std::vector<ParameterObject> inputs;
  /** Owned return identities; callback borrows never escape. */
  std::vector<ParameterObject> returns;
  /** Copy only parameter actuals, preserving compact member identity. */
  void call_arguments(const ResolvedCallArguments& arguments,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    for (const auto& value : arguments.values)
      if (const auto* parameter =
              std::get_if<ResolvedCallParameterRef>(&value.value);
          parameter && parameter->symbol_id)
        inputs.push_back(
            {*parameter->symbol_id, parameter->parameterized_index});
  }
  /** Copy the return object without retaining a borrowed reference. */
  void call_parameter_ref(const ResolvedCallParameterRef& parameter,
                          std::span<const SourceRange>,
                          checker::AddressSymbolResolutionPolicy) override {
    if (parameter.symbol_id)
      returns.push_back({*parameter.symbol_id, parameter.parameterized_index});
  }
};
/** Normalize a transfer after binding, including source aliases and dynamic indices. */
std::optional<ParameterTransfer> parameter_transfer(
    const ResolvedModule& module, const ResolvedFunction& function,
    const Instruction& instruction, SourceRange range,
    ModuleResolveDiagnostics& diagnostics) {
  bool store = false;
  const auto* address = parameter_address(instruction, store);
  if (!address)
    return std::nullopt;
  const auto* base = std::get_if<ResolvedSymbolRef>(&address->base);
  if (!base || !base->symbol_id ||
      base->symbol_id->value >= module.symbols.symbols().size())
    return std::nullopt;
  const auto& symbol = module.symbols.symbol(*base->symbol_id);
  const bool local = symbol.kind == binding::SymbolKind::CallParameter;
  const bool entry_input =
      function.is_entry && symbol.kind == binding::SymbolKind::InputParameter;
  if ((address->parameter_qualifier == ParameterAddressQualifier::Entry &&
       !entry_input) ||
      (address->parameter_qualifier == ParameterAddressQualifier::Function &&
       function.is_entry && !local)) {
    diagnostics.push_back(
        {.range = range,
         .message =
             address->parameter_qualifier == ParameterAddressQualifier::Entry
                 ? ".param::entry may access only a kernel entry input "
                   "parameter."
                 : ".param::func may access only a device-function parameter "
                   "or function-local call parameter."});
  }
  if (!local)
    return std::nullopt;
  const auto declaration = std::ranges::find_if(
      function.parameter_declarations, [&](const auto& value) {
        return value.symbol_id == *base->symbol_id &&
               value.role == ParameterDeclarationRole::BodyLocal;
      });
  if (declaration == function.parameter_declarations.end()) {
    diagnostics.push_back(
        {.range = range,
         .message = "Parameter transfer has no local owned declaration."});
    return std::nullopt;
  }
  ParameterTransfer result{
      .object = {*base->symbol_id, base->parameterized_index}, .store = store};
  if (address->named_index) {
    const auto bytes = named_array_byte_displacement(*address->named_index);
    if (!bytes || *bytes != address->named_index->byte_displacement ||
        address->offset) {
      diagnostics.push_back(
          {.range = range,
           .message =
               "Parameter transfer has inconsistent named-index bytes."});
    } else
      result.bytes = *bytes;
    if (const auto* index = std::get_if<WithLocs<ResolvedRegisterRef>>(
            &address->named_index->index))
      result.index = index->value;
  } else if (address->offset) {
    if (!address_offset_fits_signed32(*address->offset)) {
      diagnostics.push_back(
          {.range = range,
           .message = "Parameter transfer has an invalid byte offset."});
    } else {
      const auto bits = address->offset->value.bits;
      result.bytes = std::bit_cast<int64_t>(
          address->offset->operation == ResolvedAddressOffsetOperator::Subtract
              ? uint64_t{0} - bits
              : bits);
    }
  }
  return result;
}
}  // namespace

ModuleResolveDiagnostics check_call_parameter_transfers(
    const ResolvedModule& module) {
  ModuleResolveDiagnostics diagnostics;
  for (const auto& function : module.functions) {
    std::vector<std::optional<ParameterTransfer>> transfers;
    for (size_t index = 0; index < function.body.size(); ++index) {
      const auto range = index < function.instruction_ranges.size()
                             ? function.instruction_ranges[index]
                             : SourceRange{};
      transfers.push_back(function.body[index]
                              ? parameter_transfer(module, function,
                                                   *function.body[index], range,
                                                   diagnostics)
                              : std::nullopt);
    }
    for (size_t index = 0; index < transfers.size(); ++index) {
      if (!transfers[index])
        continue;
      const auto& transfer = *transfers[index];
      const auto& instruction = *function.body[index];
      const auto range = index < function.instruction_ranges.size()
                             ? function.instruction_ranges[index]
                             : SourceRange{};
      if (instruction.execution_predicate) {
        const auto& locations = instruction.execution_predicate->locs;
        diagnostics.push_back(
            {.range = locations.empty() ? range : locations.front(),
             .message = transfer.store ? "A function-local .param argument "
                                         "store cannot be predicated."
                                       : "A function-local .param return load "
                                         "cannot be predicated."});
        continue;
      }
      size_t call_index = index;
      if (transfer.store) {
        while (call_index < transfers.size() && transfers[call_index] &&
               transfers[call_index]->store)
          ++call_index;
      } else {
        while (call_index > 0 && transfers[call_index - 1] &&
               !transfers[call_index - 1]->store)
          --call_index;
        call_index = call_index > 0 ? call_index - 1 : transfers.size();
      }
      CallObjects actuals;
      const auto* call =
          call_index < function.body.size()
              ? dynamic_cast<const CallDirect*>(function.body[call_index].get())
              : nullptr;
      if (call)
        call->visit_references(actuals);
      const auto& objects = transfer.store ? actuals.inputs : actuals.returns;
      if (!call || std::ranges::find(objects, transfer.object) == objects.end())
        diagnostics.push_back(
            {.range = range,
             .message = transfer.store
                            ? "A function-local .param argument store must be "
                              "in the contiguous block immediately before a "
                              "call that uses it."
                            : "A function-local .param return load must be in "
                              "the contiguous block immediately after a call "
                              "that returns it."});
    }
  }
  return diagnostics;
}
}  // namespace ptx_frontend::resolved_ir::detail
