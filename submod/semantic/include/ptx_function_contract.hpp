#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>

namespace ptx_frontend::declaration_semantics {

/** Source-identity key retained when a numeric contract value is invalid. */
struct InvalidStructuralKey {
  /** Canonical expression spelling used to distinguish invalid declarations. */
  std::string value;

  /** Compare canonical structural identities. */
  bool operator==(const InvalidStructuralKey&) const = default;
};

/** Valid numeric ABI data or a structural identity for invalid source data. */
using NormalizedNumericValue = std::variant<uint64_t, InvalidStructuralKey>;

/** ABI-relevant parameter data independent of syntax-tree lifetime. */
struct FunctionParameterContract {
  /** Semantic state space; Invalid preserves unsupported constructed input. */
  call_argument_compatibility::CallArgumentStateSpace state_space{
      call_argument_compatibility::CallArgumentStateSpace::Invalid};
  /** Effective alignment, or an invalid structural key; absent means unspecified. */
  std::optional<NormalizedNumericValue> alignment;
  /** Modeled scalar identity, or Invalid for an unsupported source spelling. */
  base::ScalarType scalar_type{base::ScalarType::Invalid};
  /** Retained source spelling for Invalid scalar diagnostics. */
  std::string type_spelling;
  /** Whether this parameter carries pointer attributes. */
  bool is_pointer{};
  /** Pointed state space; absent is generic and Invalid is unsupported input. */
  std::optional<call_argument_compatibility::PointedStateSpace>
      pointed_state_space;
  /** Effective pointee alignment, or invalid source; absent for non-pointers. */
  std::optional<NormalizedNumericValue> pointer_alignment;
  /** Whether the parameter denotes an array. */
  bool is_array{};
  /** Array extent, or invalid source; absent is an unspecified extent. */
  std::optional<NormalizedNumericValue> array_extent;

  /** Compare ABI fields, retaining spelling identity only for invalid scalars. */
  bool operator==(const FunctionParameterContract& other) const {
    return state_space == other.state_space && alignment == other.alignment &&
           scalar_type == other.scalar_type &&
           (scalar_type != base::ScalarType::Invalid ||
            type_spelling == other.type_spelling) &&
           is_pointer == other.is_pointer &&
           pointed_state_space == other.pointed_state_space &&
           pointer_alignment == other.pointer_alignment &&
           is_array == other.is_array && array_extent == other.array_extent;
  }
};

/** Return a constant ABI value, rejecting preserved invalid structural input. */
inline std::optional<uint64_t> contract_constant(
    const std::optional<NormalizedNumericValue>& value) {
  if (!value)
    return std::nullopt;
  return std::get_if<uint64_t>(&*value)
             ? std::optional<uint64_t>{std::get<uint64_t>(*value)}
             : std::nullopt;
}

/** Convert one normalized formal contract to the shared call-ABI properties. */
inline call_argument_compatibility::CallArgumentProperties
call_argument_properties(const FunctionParameterContract& contract) {
  using call_argument_compatibility::CallArgumentProperties;
  using call_argument_compatibility::CallArgumentStateSpace;
  using call_argument_compatibility::CallArgumentVectorShape;
  const auto alignment = contract_constant(contract.alignment);
  const auto array_size = contract_constant(contract.array_extent);
  const auto pointer_alignment = contract_constant(contract.pointer_alignment);
  if (!alignment || (contract.array_extent && !array_size) ||
      (contract.is_pointer && !pointer_alignment)) {
    return {
        .state_space = CallArgumentStateSpace::Invalid,
        .scalar_type = contract.scalar_type,
        .vector_shape = CallArgumentVectorShape::Scalar,
        .type_spelling = contract.type_spelling,
        .is_array = contract.is_array,
    };
  }
  CallArgumentProperties properties{
      .state_space = contract.state_space,
      .scalar_type = contract.scalar_type,
      .vector_shape = CallArgumentVectorShape::Scalar,
      .type_spelling = contract.type_spelling,
      .array_alignment = *alignment,
      .is_array = contract.is_array,
      .array_size = array_size,
  };
  if (contract.is_pointer) {
    properties.pointer = {
        .pointed_state_space = contract.pointed_state_space,
        .pointed_alignment = *pointer_alignment,
    };
  }
  return properties;
}

/** Canonical ABI-relevant data shared by function declarations and bodies. */
struct FunctionSignature {
  /** Whether the source function is an entry point. */
  bool is_entry{};
  /** Whether the source function carries `.noreturn`. */
  bool is_noreturn{};
  /** Ordered return-parameter contracts. */
  std::vector<FunctionParameterContract> return_parameters;
  /** Ordered input-parameter contracts. */
  std::vector<FunctionParameterContract> parameters;

  /** Compare function-kind flags and ordered normalized parameter contracts. */
  bool operator==(const FunctionSignature&) const = default;
};

}  // namespace ptx_frontend::declaration_semantics
