#pragma once

#include <cstddef>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_owned_instruction.hpp>

/** Test-only adapters for typed inner variants and exact owner payloads. */
namespace test_ir_access {

/** Access any ordinary variant or tuple element by type. */
template <typename T, typename Value>
  requires requires(Value&& value) { std::get<T>(std::forward<Value>(value)); }
decltype(auto) get(Value&& value) {
  return std::get<T>(std::forward<Value>(value));
}

/** Access any ordinary variant or tuple element by index. */
template <std::size_t Index, typename Value>
  requires requires(Value&& value) {
    std::get<Index>(std::forward<Value>(value));
  }
decltype(auto) get(Value&& value) {
  return std::get<Index>(std::forward<Value>(value));
}

/** Probe an ordinary variant without changing its payload. */
template <typename T, typename Value>
  requires requires(Value* value) { std::get_if<T>(value); }
auto get_if(Value* value) {
  return std::get_if<T>(value);
}

/** Probe an ordinary variant by index. */
template <std::size_t Index, typename Value>
  requires requires(Value* value) { std::get_if<Index>(value); }
auto get_if(Value* value) {
  return std::get_if<Index>(value);
}

/** Test an ordinary variant's active alternative. */
template <typename T, typename Value>
  requires requires(const Value& value) { std::holds_alternative<T>(value); }
bool holds_alternative(const Value& value) {
  return std::holds_alternative<T>(value);
}

/** Visit ordinary variant inputs. */
template <typename Visitor, typename... Values>
  requires requires(Visitor&& visitor, Values&&... values) {
    std::visit(std::forward<Visitor>(visitor), std::forward<Values>(values)...);
  }
decltype(auto) visit(Visitor&& visitor, Values&&... values) {
  return std::visit(std::forward<Visitor>(visitor),
                    std::forward<Values>(values)...);
}

/** Borrow an exact opcode payload, throwing on a wrong opcode. */
template <ptx_frontend::resolved_ir::OwnedOpcodeRecord T>
T& get(ptx_frontend::resolved_ir::OwnedInstruction& value) {
  if (auto* payload = value.get_if<T>())
    return *payload;
  throw std::bad_variant_access();
}

/** Borrow an exact opcode payload, throwing on a wrong opcode. */
template <ptx_frontend::resolved_ir::OwnedOpcodeRecord T>
const T& get(const ptx_frontend::resolved_ir::OwnedInstruction& value) {
  if (const auto* payload = value.get_if<T>())
    return *payload;
  throw std::bad_variant_access();
}

/** Probe an owner for its exact opcode payload. */
template <ptx_frontend::resolved_ir::OwnedOpcodeRecord T>
T* get_if(ptx_frontend::resolved_ir::OwnedInstruction* value) {
  return value ? value->get_if<T>() : nullptr;
}

/** Probe an owner for its exact opcode payload. */
template <ptx_frontend::resolved_ir::OwnedOpcodeRecord T>
const T* get_if(const ptx_frontend::resolved_ir::OwnedInstruction* value) {
  return value ? value->get_if<T>() : nullptr;
}

/** Test an owner's exact opcode identity. */
template <ptx_frontend::resolved_ir::OwnedOpcodeRecord T>
bool holds_alternative(
    const ptx_frontend::resolved_ir::OwnedInstruction& value) {
  return value.get_if<T>() != nullptr;
}

}  // namespace test_ir_access
