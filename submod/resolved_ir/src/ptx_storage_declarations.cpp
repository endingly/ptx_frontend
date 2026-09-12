#include "ptx_storage_declarations.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <concepts>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "resolved_value_domains.gen.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using declaration_semantics::DeclarationDiagnostic;
using declaration_semantics::DeclarationDiagnosticKind;
using syntax_ast::AstConstantExpression;

/** Parsed source PTX version used for declaration-only compatibility rules. */
struct PtxVersion {
  /** PTX ISA major version number. */
  uint16_t major{};
  /** PTX ISA minor version number. */
  uint16_t minor{};
};

/** Parse the first source `.version` directive when the module provides one. */
std::optional<PtxVersion> module_version(const syntax_ast::AstModule& module) {
  for (const auto& item : module.items) {
    const auto* directive = std::get_if<syntax_ast::AstVersionDirective>(&item);
    if (!directive)
      continue;
    const std::string_view text = directive->version.text;
    const size_t separator = text.find('.');
    if (separator == std::string_view::npos)
      return std::nullopt;
    uint16_t major = 0;
    uint16_t minor = 0;
    const auto [major_end, major_error] =
        std::from_chars(text.data(), text.data() + separator, major);
    const auto [minor_end, minor_error] = std::from_chars(
        text.data() + separator + 1, text.data() + text.size(), minor);
    if (major_error != std::errc{} || minor_error != std::errc{} ||
        major_end != text.data() + separator ||
        minor_end != text.data() + text.size()) {
      return std::nullopt;
    }
    return PtxVersion{.major = major, .minor = minor};
  }
  return std::nullopt;
}

/** Return whether a known version predates the supplied PTX baseline. */
bool before_version(const std::optional<PtxVersion>& version, uint16_t major,
                    uint16_t minor) {
  return version && (version->major < major ||
                     (version->major == major && version->minor < minor));
}

/** Append one declaration-normalization diagnostic without source ownership. */
void diagnose(std::vector<DeclarationDiagnostic>& diagnostics,
              DeclarationDiagnosticKind kind, SourceRange range,
              std::string message) {
  diagnostics.push_back(
      {.kind = kind, .range = range, .message = std::move(message)});
}

/** Convert a syntax state space to the public storage subset. */
std::optional<StorageSpace> storage_space(syntax_ast::AstStateSpace space) {
  switch (space) {
    case syntax_ast::AstStateSpace::Global:
      return StorageSpace::Global;
    case syntax_ast::AstStateSpace::Constant:
      return StorageSpace::Constant;
    case syntax_ast::AstStateSpace::Shared:
      return StorageSpace::Shared;
    case syntax_ast::AstStateSpace::Local:
      return StorageSpace::Local;
    case syntax_ast::AstStateSpace::Register:
    case syntax_ast::AstStateSpace::Parameter:
      return std::nullopt;
  }
  return std::nullopt;
}

/** Look up one generated scalar spelling, including its source leading dot. */
std::optional<base::ScalarType> scalar_type(std::string_view spelling) {
  if (!spelling.starts_with('.'))
    return std::nullopt;
  const std::string_view suffix = spelling.substr(1);
  const auto found = std::ranges::find_if(
      generated_detail::kScalarTypes,
      [suffix](const auto& entry) { return entry.suffix == suffix; });
  return found == generated_detail::kScalarTypes.end()
             ? std::nullopt
             : std::optional{found->value};
}

/** Return whether a scalar is a PTX fundamental storage element type. */
bool is_fundamental_storage_scalar(base::ScalarType type) {
  switch (type) {
    case base::ScalarType::U8:
    case base::ScalarType::U16:
    case base::ScalarType::U32:
    case base::ScalarType::U64:
    case base::ScalarType::S8:
    case base::ScalarType::S16:
    case base::ScalarType::S32:
    case base::ScalarType::S64:
    case base::ScalarType::B8:
    case base::ScalarType::B16:
    case base::ScalarType::B32:
    case base::ScalarType::B64:
    case base::ScalarType::B128:
    case base::ScalarType::F16:
    case base::ScalarType::F16x2:
    case base::ScalarType::F32:
    case base::ScalarType::F64:
      return true;
    case base::ScalarType::Invalid:
    case base::ScalarType::U8x4:
    case base::ScalarType::U16x2:
    case base::ScalarType::S8x4:
    case base::ScalarType::S16x2:
    case base::ScalarType::F32x2:
    case base::ScalarType::BF16:
    case base::ScalarType::BF16x2:
    case base::ScalarType::E4m3x2:
    case base::ScalarType::E5m2x2:
    case base::ScalarType::Pred:
    case base::ScalarType::TF32:
    case base::ScalarType::E4m3:
    case base::ScalarType::E5m2:
      return false;
  }
  return false;
}

/** Classify PTX opaque declaration identities without inventing a byte layout. */
std::optional<StorageOpaqueType> opaque_type(std::string_view spelling) {
  if (spelling == ".texref")
    return StorageOpaqueType::Texture;
  if (spelling == ".samplerref")
    return StorageOpaqueType::Sampler;
  if (spelling == ".surfref")
    return StorageOpaqueType::Surface;
  return std::nullopt;
}

/** Return whether a bound variable has an opaque PTX object declaration type. */
bool is_opaque_object(const binding::Symbol& symbol) {
  return symbol.type && opaque_type(*symbol.type).has_value();
}

/** Decode a source magnitude independently of its declaration's range policy. */
std::optional<uint64_t> unsigned_value(std::string_view text) {
  return base::parseIntegerMagnitude(text);
}

/** Find the exact bound symbol created for one source declarator. */
std::optional<binding::SymbolId> declaration_symbol(
    const binding::SymbolTable& symbols, binding::ScopeId scope,
    const syntax_ast::AstVariableDeclarator& declarator) {
  return symbols.exactDeclaration(scope, declarator.name.syntax.text,
                                  declarator.parameterized_count.has_value());
}

/** Find the nearest owning function for a lexical declaration scope. */
std::optional<binding::SymbolId> owner_function(
    const binding::SymbolTable& symbols, binding::ScopeId scope) {
  for (;;) {
    const binding::Scope& current = symbols.scope(scope);
    if (current.kind == binding::ScopeKind::Function)
      return current.owner;
    if (!current.parent)
      return std::nullopt;
    scope = *current.parent;
  }
}

/** Multiply two byte/layout factors without wrapping a public extent. */
std::optional<uint64_t> checked_product(uint64_t left, uint64_t right) {
  if (right != 0 && left > std::numeric_limits<uint64_t>::max() / right)
    return std::nullopt;
  return left * right;
}

/** Return the vector lane count encoded by a declaration's optional modifier. */
uint8_t vector_width(const std::optional<syntax_ast::AstSyntax>& vector_type) {
  if (!vector_type)
    return 1;
  return vector_type->text == ".v2" ? 2 : 4;
}

/** Decode the linkage written on this source declaration occurrence. */
binding::SymbolLinkage declaration_linkage(
    const std::vector<syntax_ast::AstSyntax>& qualifiers) {
  for (const auto& qualifier : qualifiers) {
    if (qualifier.text == ".extern")
      return binding::SymbolLinkage::External;
    if (qualifier.text == ".visible")
      return binding::SymbolLinkage::Visible;
    if (qualifier.text == ".weak")
      return binding::SymbolLinkage::Weak;
  }
  return binding::SymbolLinkage::None;
}

/** Return whether a mask selects exactly one byte of a 64-bit address. */
bool is_byte_mask(uint64_t mask) {
  for (uint8_t byte = 0; byte < sizeof(uint64_t); ++byte) {
    if (mask == (uint64_t{0xff} << (byte * 8)))
      return true;
  }
  return false;
}

/** Get the list length needed to infer an omitted first array extent. */
std::optional<uint64_t> inferred_outer_extent(
    const std::optional<syntax_ast::AstInitializer>& initializer) {
  if (!initializer)
    return std::nullopt;
  const auto* list =
      std::get_if<syntax_ast::AstInitializerList>(&initializer->value);
  if (!list)
    return std::nullopt;
  return static_cast<uint64_t>(list->elements.size());
}

/** Decode validated source attributes into the declaration's owned metadata. */
bool resolve_attributes(const syntax_ast::AstVariableDeclaration& declaration,
                        bool& is_managed,
                        std::optional<ResolvedUnifiedId>& unified_id,
                        std::vector<DeclarationDiagnostic>& diagnostics) {
  for (const auto& attribute : declaration.attributes) {
    if (attribute.kind == syntax_ast::AstAttributeKind::Managed) {
      is_managed = true;
      continue;
    }
    if (attribute.values.size() != 2) {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
          attribute.range, ".unified requires exactly two integer values.");
      return false;
    }
    const auto upper = unsigned_value(attribute.values[0].text);
    const auto lower = unsigned_value(attribute.values[1].text);
    if (!upper || !lower) {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
          attribute.range, ".unified values must be unsigned 64-bit integers.");
      return false;
    }
    unified_id = ResolvedUnifiedId{.upper = *upper, .lower = *lower};
  }
  return true;
}

/** Evaluate an integer expression into a declaration element's stored bits. */
std::optional<StorageConstant> integer_constant(
    const AstConstantExpression& expression, base::ScalarType type,
    const std::optional<PtxVersion>& version,
    std::vector<DeclarationDiagnostic>& diagnostics) {
  const base::ScalarKind kind = base::scalar_kind(type);
  if (kind != base::ScalarKind::Unsigned && kind != base::ScalarKind::Signed &&
      kind != base::ScalarKind::Bit) {
    return std::nullopt;
  }
  auto value = declaration_semantics::constantIntegerValue(expression);
  if (!value) {
    const auto* call =
        std::get_if<syntax_ast::AstConstantCall>(&expression.node);
    const auto* mask =
        call == nullptr
            ? nullptr
            : std::get_if<syntax_ast::AstConstantLiteral>(&call->callee->node);
    if (mask == nullptr ||
        mask->value.kind != syntax_ast::AstImmediateKind::HexInteger) {
      return std::nullopt;
    }
    const auto mask_value =
        declaration_semantics::constantIntegerValue(*call->callee);
    const auto argument =
        declaration_semantics::constantIntegerValue(*call->argument);
    if (!mask_value || !is_byte_mask(mask_value->bits) || !argument)
      return std::nullopt;
    if (before_version(version, 7, 3)) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageInitializer,
               expression.range,
               "Integer mask initializers require PTX ISA >= 7.3.");
      return std::nullopt;
    }
    value = declaration_semantics::IntegerConstantValue{
        .bits = (argument->bits & mask_value->bits) >>
                std::countr_zero(mask_value->bits),
        .is_unsigned = true};
  }
  const uint8_t byte_size = base::scalar_size_of(type);
  if (type == base::ScalarType::B128) {
    // Widen the evaluated result, not its operands: PTX expressions stay 64-bit.
    const bool negative = !value->is_unsigned && (value->bits >> 63) != 0;
    return StorageConstant{
        .bits = value->bits,
        .high_bits = negative ? std::numeric_limits<uint64_t>::max() : 0,
    };
  }
  if (byte_size == 0 || byte_size > sizeof(uint64_t)) {
    diagnose(
        diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
        expression.range,
        "Integer initializer type is not representable as a storage constant.");
    return std::nullopt;
  }

  const uint8_t bit_width = byte_size * 8;
  const uint64_t mask = bit_width == 64 ? std::numeric_limits<uint64_t>::max()
                                        : (uint64_t{1} << bit_width) - 1;
  return StorageConstant{.bits = value->bits & mask};
}

/** Resolve a direct floating literal or a signed direct floating literal. */
std::optional<StorageConstant> floating_literal(
    const AstConstantExpression& expression, base::ScalarType type,
    std::vector<DeclarationDiagnostic>& diagnostics) {
  const AstConstantExpression* current = &expression;
  while (
      const auto* parenthesized =
          std::get_if<syntax_ast::AstConstantParenthesized>(&current->node)) {
    current = parenthesized->expression.get();
  }
  const syntax_ast::AstImmediate* immediate = nullptr;
  bool negate = false;
  if (const auto* literal =
          std::get_if<syntax_ast::AstConstantLiteral>(&current->node)) {
    immediate = &literal->value;
  } else if (const auto* unary =
                 std::get_if<syntax_ast::AstConstantUnary>(&current->node)) {
    if (unary->operation != syntax_ast::AstConstantUnaryOperator::Plus &&
        unary->operation != syntax_ast::AstConstantUnaryOperator::Minus) {
      return std::nullopt;
    }
    const AstConstantExpression* operand = unary->operand.get();
    while (
        const auto* parenthesized =
            std::get_if<syntax_ast::AstConstantParenthesized>(&operand->node)) {
      operand = parenthesized->expression.get();
    }
    const auto* literal =
        std::get_if<syntax_ast::AstConstantLiteral>(&operand->node);
    if (!literal)
      return std::nullopt;
    immediate = &literal->value;
    negate = unary->operation == syntax_ast::AstConstantUnaryOperator::Minus;
  }
  if (!immediate ||
      (immediate->kind != syntax_ast::AstImmediateKind::F32Hex &&
       immediate->kind != syntax_ast::AstImmediateKind::F64Hex &&
       immediate->kind != syntax_ast::AstImmediateKind::DecimalFloat)) {
    return std::nullopt;
  }
  syntax_ast::AstImmediate signed_immediate = *immediate;
  const bool bit_pattern =
      immediate->kind == syntax_ast::AstImmediateKind::F32Hex ||
      immediate->kind == syntax_ast::AstImmediateKind::F64Hex;
  if (negate && !bit_pattern)
    signed_immediate.syntax.text = "-" + signed_immediate.syntax.text;
  const auto resolved = resolve_immediate_literal(signed_immediate, type);
  if (!resolved) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageInitializer,
             expression.range, resolved.error().message);
    return std::nullopt;
  }
  uint64_t bits = resolved->bits;
  if (negate && bit_pattern) {
    if (type == base::ScalarType::F32)
      bits ^= uint64_t{1} << 31;
    else if (type == base::ScalarType::F64)
      bits ^= uint64_t{1} << 63;
    else {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
          expression.range,
          "Floating bit-pattern sign requires an F32 or F64 storage type.");
      return std::nullopt;
    }
  }
  return StorageConstant{.bits = bits};
}

/** Resolve one permitted initializer symbol as a non-simulated relocation. */
std::optional<StorageRelocation> symbol_relocation(
    const syntax_ast::AstConstantSymbol& symbol,
    const binding::SymbolTable& symbols,
    const std::optional<PtxVersion>& version,
    std::vector<DeclarationDiagnostic>& diagnostics) {
  const binding::SymbolReference* reference =
      symbols.initializerReference(symbol.name.syntax.range);
  if (reference == nullptr || !reference->target) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageInitializer,
             symbol.name.syntax.range,
             fmt::format("Initializer symbol '{}' is not bound.",
                         symbol.name.syntax.text));
    return std::nullopt;
  }
  const binding::SymbolLookup& lookup = *reference->target;
  const binding::Symbol& target = symbols.symbol(lookup.symbol);
  StorageAddressKind address_kind = StorageAddressKind::StateSpace;
  if (target.kind == binding::SymbolKind::Function) {
    if (target.function_is_entry && before_version(version, 3, 1)) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageInitializer,
               symbol.name.syntax.range,
               "Kernel function initializers require PTX 3.1 or later.");
      return std::nullopt;
    }
    address_kind = StorageAddressKind::Function;
  } else if (target.kind != binding::SymbolKind::Variable ||
             is_opaque_object(target) ||
             (target.state_space != syntax_ast::AstStateSpace::Global &&
              target.state_space != syntax_ast::AstStateSpace::Constant)) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageInitializer,
             symbol.name.syntax.range,
             fmt::format("Initializer symbol '{}' is not addressable storage.",
                         symbol.name.syntax.text));
    return std::nullopt;
  }
  if (target.state_space == syntax_ast::AstStateSpace::Global &&
      before_version(version, 3, 1)) {
    address_kind = StorageAddressKind::Generic;
  }
  return StorageRelocation{
      .symbol_id = target.id,
      .parameterized_index = lookup.parameterized_index,
      .address_kind = address_kind,
  };
}

/** Resolve address-valued initializer syntax into one stable symbol relocation. */
std::optional<StorageRelocation> relocation(
    const AstConstantExpression& expression,
    const binding::SymbolTable& symbols,
    const std::optional<PtxVersion>& version,
    std::vector<DeclarationDiagnostic>& diagnostics) {
  return std::visit(
      [&](const auto& value) -> std::optional<StorageRelocation> {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, syntax_ast::AstConstantSymbol>) {
          return symbol_relocation(value, symbols, version, diagnostics);
        } else if constexpr (std::same_as<
                                 Value, syntax_ast::AstConstantParenthesized>) {
          return relocation(*value.expression, symbols, version, diagnostics);
        } else if constexpr (std::same_as<Value, syntax_ast::AstConstantCall>) {
          const auto* callee_symbol =
              std::get_if<syntax_ast::AstConstantSymbol>(&value.callee->node);
          if (callee_symbol && callee_symbol->name.syntax.text == "generic") {
            auto result =
                relocation(*value.argument, symbols, version, diagnostics);
            if (result && result->byte_mask) {
              diagnose(
                  diagnostics,
                  DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                  expression.range,
                  "generic() cannot be applied after an address byte mask.");
              return std::nullopt;
            }
            if (result &&
                result->address_kind == StorageAddressKind::Function) {
              diagnose(diagnostics,
                       DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                       expression.range,
                       "generic() cannot be applied to a function address.");
              return std::nullopt;
            }
            if (result)
              result->address_kind = StorageAddressKind::Generic;
            return result;
          }
          const auto* mask =
              std::get_if<syntax_ast::AstConstantLiteral>(&value.callee->node);
          if (!mask ||
              mask->value.kind != syntax_ast::AstImmediateKind::HexInteger)
            return std::nullopt;
          if (before_version(version, 7, 1)) {
            diagnose(diagnostics,
                     DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                     expression.range,
                     "Address mask initializers require PTX ISA >= 7.1.");
            return std::nullopt;
          }
          const auto mask_value =
              declaration_semantics::constantIntegerValue(*value.callee);
          if (!mask_value)
            return std::nullopt;
          if (!is_byte_mask(mask_value->bits)) {
            diagnose(diagnostics,
                     DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                     expression.range,
                     "Address mask initializer must select exactly one byte.");
            return std::nullopt;
          }
          auto result =
              relocation(*value.argument, symbols, version, diagnostics);
          if (!result)
            return std::nullopt;
          if (result->byte_mask) {
            diagnose(diagnostics,
                     DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                     expression.range,
                     "Nested initializer address masks are not representable.");
            return std::nullopt;
          }
          result->byte_mask = mask_value->bits;
          return result;
        } else if constexpr (std::same_as<Value,
                                          syntax_ast::AstConstantBinary>) {
          using Operator = syntax_ast::AstConstantBinaryOperator;
          if (value.operation != Operator::Add &&
              value.operation != Operator::Subtract) {
            return std::nullopt;
          }
          auto left = relocation(*value.left, symbols, version, diagnostics);
          const auto right =
              declaration_semantics::constantIntegerValue(*value.right);
          if (left && right) {
            if (left->address_kind == StorageAddressKind::Function) {
              diagnose(diagnostics,
                       DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                       expression.range,
                       "Address arithmetic cannot be applied to a function "
                       "address.");
              return std::nullopt;
            }
            if (left->byte_mask) {
              diagnose(diagnostics,
                       DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                       expression.range,
                       "Address arithmetic cannot be applied after an address "
                       "byte mask.");
              return std::nullopt;
            }
            left->addend_bits += value.operation == Operator::Add
                                     ? right->bits
                                     : uint64_t{0} - right->bits;
            return left;
          }
          if (value.operation == Operator::Add) {
            auto right_relocation =
                relocation(*value.right, symbols, version, diagnostics);
            const auto left_integer =
                declaration_semantics::constantIntegerValue(*value.left);
            if (right_relocation && left_integer) {
              if (right_relocation->address_kind ==
                  StorageAddressKind::Function) {
                diagnose(
                    diagnostics,
                    DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                    expression.range,
                    "Address arithmetic cannot be applied to a function "
                    "address.");
                return std::nullopt;
              }
              if (right_relocation->byte_mask) {
                diagnose(
                    diagnostics,
                    DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                    expression.range,
                    "Address arithmetic cannot be applied after an address "
                    "byte mask.");
                return std::nullopt;
              }
              right_relocation->addend_bits += left_integer->bits;
              return right_relocation;
            }
          }
          return std::nullopt;
        } else {
          return std::nullopt;
        }
      },
      expression.node);
}

/** Resolve one scalar initializer leaf without fabricating an unsupported value. */
std::optional<std::variant<StorageConstant, StorageRelocation>>
initializer_value(const AstConstantExpression& expression,
                  base::ScalarType type, const binding::SymbolTable& symbols,
                  const std::optional<PtxVersion>& version,
                  std::vector<DeclarationDiagnostic>& diagnostics) {
  if (const auto integer =
          integer_constant(expression, type, version, diagnostics))
    return std::variant<StorageConstant, StorageRelocation>{*integer};
  if (const auto floating = floating_literal(expression, type, diagnostics))
    return std::variant<StorageConstant, StorageRelocation>{*floating};
  if (const auto address =
          relocation(expression, symbols, version, diagnostics)) {
    const bool address_width =
        type == base::ScalarType::U32 || type == base::ScalarType::U64;
    const bool masked_byte = address->byte_mask && type == base::ScalarType::U8;
    if (!address_width && !masked_byte) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageInitializer,
               expression.range,
               "A storage relocation requires a .u32/.u64 destination or a "
               "masked .u8 destination.");
      return std::nullopt;
    }
    return std::variant<StorageConstant, StorageRelocation>{*address};
  }
  diagnose(diagnostics,
           DeclarationDiagnosticKind::UnsupportedStorageInitializer,
           expression.range,
           "Initializer expression cannot be normalized as a constant or "
           "relocation.");
  return std::nullopt;
}

/** Return the scalar leaf count below one aggregate dimension. */
std::optional<uint64_t> scalar_stride(std::span<const uint64_t> shape,
                                      size_t start) {
  uint64_t stride = 1;
  for (size_t index = start; index < shape.size(); ++index) {
    const auto product = checked_product(stride, shape[index]);
    if (!product)
      return std::nullopt;
    stride = *product;
  }
  return stride;
}

/** Flatten brace-structured initializer leaves into scalar declaration offsets. */
bool flatten_initializer(const syntax_ast::AstInitializer& initializer,
                         std::span<const uint64_t> shape, size_t depth,
                         uint64_t scalar_index, uint64_t scalar_bytes,
                         base::ScalarType type,
                         const binding::SymbolTable& symbols,
                         const std::optional<PtxVersion>& version,
                         std::vector<StorageInitializerElement>& output,
                         std::vector<DeclarationDiagnostic>& diagnostics) {
  if (depth == shape.size()) {
    const auto* expression =
        std::get_if<AstConstantExpression>(&initializer.value);
    if (!expression) {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
          initializer.range, "Scalar initializer leaf cannot be a brace list.");
      return false;
    }
    const auto byte_offset = checked_product(scalar_index, scalar_bytes);
    if (!byte_offset) {
      diagnose(diagnostics, DeclarationDiagnosticKind::StorageExtentOverflow,
               initializer.range,
               "Initializer byte offset overflows uint64_t.");
      return false;
    }
    const auto value =
        initializer_value(*expression, type, symbols, version, diagnostics);
    if (!value)
      return false;
    output.push_back({.byte_offset = *byte_offset,
                      .value = std::move(*value),
                      .range = expression->range});
    return true;
  }

  const auto* list =
      std::get_if<syntax_ast::AstInitializerList>(&initializer.value);
  if (!list) {
    diagnose(
        diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
        initializer.range,
        "Aggregate initializer does not match the resolved declaration shape.");
    return false;
  }
  const auto stride = scalar_stride(shape, depth + 1);
  if (!stride) {
    diagnose(diagnostics, DeclarationDiagnosticKind::StorageExtentOverflow,
             list->range, "Initializer scalar stride overflows uint64_t.");
    return false;
  }
  if (list->elements.size() > shape[depth]) {
    diagnose(
        diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
        list->range, "Initializer has more elements than its resolved extent.");
    return false;
  }
  for (size_t index = 0; index < list->elements.size(); ++index) {
    const auto offset = checked_product(static_cast<uint64_t>(index), *stride);
    if (!offset ||
        scalar_index > std::numeric_limits<uint64_t>::max() - *offset) {
      diagnose(diagnostics, DeclarationDiagnosticKind::StorageExtentOverflow,
               list->elements[index].range,
               "Initializer scalar index overflows uint64_t.");
      return false;
    }
    if (!flatten_initializer(list->elements[index], shape, depth + 1,
                             scalar_index + *offset, scalar_bytes, type,
                             symbols, version, output, diagnostics)) {
      return false;
    }
  }
  return true;
}

/** Build one owned storage record for a declaration declarator in one scope. */
void resolve_declarator(const syntax_ast::AstVariableDeclaration& declaration,
                        const syntax_ast::AstVariableDeclarator& declarator,
                        binding::ScopeId scope,
                        const binding::SymbolTable& symbols,
                        const std::optional<PtxVersion>& version,
                        std::vector<ResolvedStorageDeclaration>& declarations,
                        std::vector<DeclarationDiagnostic>& diagnostics) {
  const auto space = storage_space(declaration.state_space);
  if (!space)
    return;
  const auto symbol_id = declaration_symbol(symbols, scope, declarator);
  if (!symbol_id) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
             declarator.range, "Storage declarator has no bound symbol.");
    return;
  }
  const binding::Symbol& symbol = symbols.symbol(*symbol_id);
  const binding::SymbolLinkage source_linkage =
      declaration_linkage(declaration.qualifiers);
  const bool external = source_linkage == binding::SymbolLinkage::External;
  const uint8_t lanes = vector_width(declaration.vector_type);

  std::optional<base::ScalarType> scalar = scalar_type(declaration.type.text);
  std::optional<StorageOpaqueType> opaque = opaque_type(declaration.type.text);
  if (!scalar && !opaque) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
             declaration.type.range,
             fmt::format("Storage type '{}' has no modeled element identity.",
                         declaration.type.text));
    return;
  }
  if (scalar && *scalar == base::ScalarType::Pred) {
    diagnose(
        diagnostics, DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
        declaration.type.range, ".pred cannot declare addressable storage.");
    return;
  }
  if (scalar && !is_fundamental_storage_scalar(*scalar)) {
    diagnose(diagnostics,
             DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
             declaration.type.range,
             fmt::format("Storage type '{}' is not a fundamental PTX storage "
                         "element type.",
                         declaration.type.text));
    return;
  }
  if (opaque &&
      (scope != symbols.moduleScope() || *space != StorageSpace::Global ||
       declaration.vector_type || !declarator.array_dimensions.empty() ||
       declarator.parameterized_count)) {
    diagnose(
        diagnostics, DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
        declarator.range,
        "Opaque storage is supported only as a module-scope, scalar .global "
        "declaration with one named object.");
    return;
  }

  std::vector<std::optional<uint64_t>> extents;
  extents.reserve(declarator.array_dimensions.size());
  for (size_t index = 0; index < declarator.array_dimensions.size(); ++index) {
    const auto& dimension = declarator.array_dimensions[index];
    if (!dimension.size) {
      const auto inferred = index == 0
                                ? inferred_outer_extent(declarator.initializer)
                                : std::nullopt;
      if (!inferred && !(index == 0 && external)) {
        diagnose(diagnostics,
                 DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
                 dimension.range,
                 "Only an external first array dimension may remain unsized.");
        return;
      }
      extents.push_back(inferred);
      continue;
    }
    const auto extent =
        declaration_semantics::constantArrayExtent(*dimension.size);
    if (!extent || *extent == 0) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
               dimension.range,
               "Storage array extent is not a positive integer constant.");
      return;
    }
    extents.push_back(*extent);
  }

  bool is_managed = false;
  std::optional<ResolvedUnifiedId> unified_id;
  if (!resolve_attributes(declaration, is_managed, unified_id, diagnostics))
    return;

  std::optional<uint64_t> explicit_alignment;
  if (declaration.alignment) {
    explicit_alignment = unsigned_value(declaration.alignment->text);
    if (!explicit_alignment) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
               declaration.alignment->range,
               "Storage alignment is not an unsigned integer.");
      return;
    }
  }

  std::optional<uint64_t> byte_extent;
  std::optional<uint64_t> alignment = explicit_alignment;
  uint64_t scalar_bytes = 0;
  if (scalar) {
    scalar_bytes = base::scalar_size_of(*scalar);
    if (scalar_bytes == 0) {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
          declaration.type.range, "Storage scalar type has no byte width.");
      return;
    }
    const auto lane_bytes = checked_product(scalar_bytes, lanes);
    if (!lane_bytes || *lane_bytes > 16) {
      diagnose(diagnostics,
               DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
               declaration.range,
               "Storage vector width exceeds the supported 128-bit PTX limit.");
      return;
    }
    if (!alignment)
      alignment = *lane_bytes;
    uint64_t known_extent = *lane_bytes;
    bool has_unsized_extent = false;
    for (const auto extent : extents) {
      if (!extent) {
        has_unsized_extent = true;
        continue;
      }
      const auto product = checked_product(known_extent, *extent);
      if (!product) {
        diagnose(diagnostics, DeclarationDiagnosticKind::StorageExtentOverflow,
                 declarator.range, "Storage byte extent overflows uint64_t.");
        return;
      }
      known_extent = *product;
    }
    byte_extent = has_unsized_extent ? std::nullopt
                                     : std::optional<uint64_t>{known_extent};
  }

  ResolvedStorageDeclaration resolved{
      .symbol_id = *symbol_id,
      .scope_id = scope,
      .owner_function = owner_function(symbols, scope),
      .space = *space,
      .element_type =
          scalar ? StorageElementType{*scalar} : StorageElementType{*opaque},
      .vector_width = lanes,
      .array_extents = extents,
      .byte_extent = byte_extent,
      .alignment = alignment,
      .explicit_alignment = explicit_alignment,
      .linkage = source_linkage,
      .declaration_kind = external ? StorageDeclarationKind::External
                                   : StorageDeclarationKind::Definition,
      .is_dynamic_shared = external && *space == StorageSpace::Shared &&
                           !extents.empty() && !extents.front(),
      .parameterized_count = symbol.parameterized_count,
      .is_managed = is_managed,
      .unified_id = unified_id,
      .initialization =
          external  ? StorageInitializationKind::External
          : !scalar ? StorageInitializationKind::Uninitialized
          : (*space == StorageSpace::Global || *space == StorageSpace::Constant)
              ? StorageInitializationKind::Zero
              : StorageInitializationKind::Uninitialized,
      .range = declarator.range,
  };

  if (declarator.initializer) {
    if (!scalar) {
      diagnose(
          diagnostics, DeclarationDiagnosticKind::UnsupportedStorageInitializer,
          declarator.initializer->range,
          "Opaque storage initializers have no representable scalar layout.");
      return;
    }
    std::vector<uint64_t> shape;
    shape.reserve(extents.size() + (lanes > 1 ? 1 : 0));
    for (const auto extent : extents) {
      if (!extent) {
        diagnose(diagnostics,
                 DeclarationDiagnosticKind::UnsupportedStorageInitializer,
                 declarator.initializer->range,
                 "An initializer requires a fully known storage shape.");
        return;
      }
      shape.push_back(*extent);
    }
    if (lanes > 1)
      shape.push_back(lanes);
    if (!flatten_initializer(*declarator.initializer, shape, 0, 0, scalar_bytes,
                             *scalar, symbols, version, resolved.initializer,
                             diagnostics)) {
      return;
    }
    resolved.initialization = StorageInitializationKind::Explicit;
  }
  declarations.push_back(std::move(resolved));
}

/** Traverse a function body while retaining the binding's lexical block scopes. */
void resolve_body(const std::vector<syntax_ast::AstFunctionBodyItem>& body,
                  binding::ScopeId scope, const binding::SymbolTable& symbols,
                  const std::optional<PtxVersion>& version,
                  std::vector<ResolvedStorageDeclaration>& declarations,
                  std::vector<DeclarationDiagnostic>& diagnostics) {
  for (const auto& item : body) {
    if (const auto* declaration =
            std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      for (const auto& declarator : declaration->declarators) {
        resolve_declarator(*declaration, declarator, scope, symbols, version,
                           declarations, diagnostics);
      }
    } else if (const auto* block =
                   std::get_if<std::unique_ptr<syntax_ast::AstBlock>>(&item);
               block != nullptr && *block) {
      const auto child_scope = symbols.blockScope(scope, (*block)->range);
      if (!child_scope) {
        diagnose(diagnostics,
                 DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
                 (*block)->range, "Storage block has no bound lexical scope.");
        return;
      }
      resolve_body((*block)->body, *child_scope, symbols, version, declarations,
                   diagnostics);
    }
  }
}

}  // namespace

std::expected<std::vector<ResolvedStorageDeclaration>,
              std::vector<DeclarationDiagnostic>>
resolve_storage_declarations(const syntax_ast::AstModule& module,
                             const binding::SymbolTable& symbols) {
  std::vector<ResolvedStorageDeclaration> declarations;
  std::vector<DeclarationDiagnostic> diagnostics;
  const auto version = module_version(module);

  for (const auto& item : module.items) {
    if (const auto* declaration =
            std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      for (const auto& declarator : declaration->declarators) {
        resolve_declarator(*declaration, declarator, symbols.moduleScope(),
                           symbols, version, declarations, diagnostics);
      }
    } else if (const auto* function =
                   std::get_if<syntax_ast::AstFunction>(&item)) {
      const auto function_scope = symbols.functionScope(function->range);
      if (!function_scope) {
        diagnose(diagnostics,
                 DeclarationDiagnosticKind::UnsupportedStorageDeclaration,
                 function->range, "Function has no bound lexical scope.");
        continue;
      }
      resolve_body(function->body, *function_scope, symbols, version,
                   declarations, diagnostics);
    }
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return declarations;
}

}  // namespace ptx_frontend::resolved_ir
